using System.IO.Compression;
using Microsoft.Data.Sqlite;
using Microsoft.EntityFrameworkCore;
using Pockettransfer.Server.Data;
using Pockettransfer.Server.Services;
using Xunit;

namespace Pockettransfer.Tests;

public sealed class DatabaseBackupTests : IDisposable
{
    private readonly SqliteConnection _conn = new("Data Source=:memory:");
    private readonly string _root = Directory.CreateTempSubdirectory("ptbak").FullName;

    public DatabaseBackupTests()
    {
        _conn.Open();
        using var db = CreateDb();
        db.Database.EnsureCreated();
    }

    [Fact]
    public async Task ExportImport_RoundTripsUsersBoxesAndPokemon()
    {
        using (var db = CreateDb())
        {
            Seed(db, holdSession: true);
            await db.SaveChangesAsync();
        }

        byte[] dump;
        using (var db = CreateDb())
        {
            var svc = new DatabaseBackupService(db, new DataPaths(_root));
            dump = (await svc.ExportAsync(CancellationToken.None)).Bytes;
        }

        using (var db = CreateDb())
        {
            db.BankPokemon.RemoveRange(db.BankPokemon);
            db.BankBoxes.RemoveRange(db.BankBoxes);
            db.Users.RemoveRange(db.Users);
            await db.SaveChangesAsync();
            Assert.Equal(0, await db.Users.CountAsync());

            var svc = new DatabaseBackupService(db, new DataPaths(_root));
            await using var stream = new MemoryStream(dump);
            await svc.ImportAsync(stream, DatabaseBackupService.ConfirmPhrase, CancellationToken.None);

            var user = await db.Users.SingleAsync();
            Assert.Equal("ash", user.Username);
            Assert.True(user.IsAdmin);
            var box = await db.BankBoxes.SingleAsync();
            Assert.Equal("Box 1", box.Name);
            var pk = await db.BankPokemon.SingleAsync();
            Assert.Equal(25, pk.Species);
            Assert.Equal(new byte[] { 1, 2, 3, 4 }, pk.PkmData);
            Assert.Null(pk.HeldBySessionId);
            Assert.True(pk.Committed);
        }
    }

    [Fact]
    public async Task CreateBackup_ThenRestore_ReplacesLiveData()
    {
        using (var db = CreateDb())
        {
            Seed(db);
            await db.SaveChangesAsync();
        }

        string name;
        using (var db = CreateDb())
        {
            var svc = new DatabaseBackupService(db, new DataPaths(_root));
            name = (await svc.CreateBackupAsync(CancellationToken.None)).Name;
            Assert.True(DatabaseBackupService.IsSafeBackupName(name));
            Assert.Single(svc.ListBackups());
        }

        using (var db = CreateDb())
        {
            db.Users.Add(new User
            {
                Username = "later",
                PasswordHash = "x",
                IsAdmin = true,
                CreatedAt = DateTimeOffset.UtcNow,
            });
            await db.SaveChangesAsync();
            Assert.Equal(2, await db.Users.CountAsync());

            var svc = new DatabaseBackupService(db, new DataPaths(_root));
            await svc.RestoreFromBackupAsync(name, DatabaseBackupService.ConfirmPhrase, CancellationToken.None);
            Assert.Equal("ash", (await db.Users.SingleAsync()).Username);
            Assert.Contains(svc.ListBackups(), b => b.Name.StartsWith("pre-restore-", StringComparison.Ordinal));
        }
    }

    [Fact]
    public async Task Import_RejectsWrongConfirmAndBadVersion()
    {
        using var db = CreateDb();
        var svc = new DatabaseBackupService(db, new DataPaths(_root));
        await using var empty = new MemoryStream([0x1F, 0x8B]);
        var wrong = await Assert.ThrowsAsync<InvalidOperationException>(
            () => svc.ImportAsync(empty, "nope", CancellationToken.None));
        Assert.Contains("RESTORE", wrong.Message, StringComparison.Ordinal);

        var json = """{"version":99,"exportedAt":"2026-01-01T00:00:00Z","engine":"SQLite","users":[],"devices":[],"pairingCodes":[],"boxes":[],"pokemon":[]}"""u8.ToArray();
        await using var stream = new MemoryStream(Gzip(json));
        var bad = await Assert.ThrowsAsync<InvalidOperationException>(
            () => svc.ImportAsync(stream, DatabaseBackupService.ConfirmPhrase, CancellationToken.None));
        Assert.Contains("version", bad.Message, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void SafeBackupName_RejectsTraversal()
    {
        Assert.False(DatabaseBackupService.IsSafeBackupName("../evil.ptbak"));
        Assert.False(DatabaseBackupService.IsSafeBackupName("a/b.ptbak"));
        Assert.False(DatabaseBackupService.IsSafeBackupName("a.ptbak.exe"));
        Assert.True(DatabaseBackupService.IsSafeBackupName("pockettransfer-20260822-010203.ptbak"));
    }

    [Fact]
    public void ReadBackup_RejectsUnknownFile()
    {
        using var db = CreateDb();
        var svc = new DatabaseBackupService(db, new DataPaths(_root));
        Assert.Throws<InvalidOperationException>(() => svc.DeleteBackup("missing.ptbak"));
        Assert.Throws<InvalidOperationException>(() => svc.DeleteBackup("../x.ptbak"));
    }

    public void Dispose()
    {
        _conn.Dispose();
        try { Directory.Delete(_root, recursive: true); }
        catch { /* temp cleanup */ }
    }

    private BankDbContext CreateDb()
    {
        var options = new DbContextOptionsBuilder<BankDbContext>().UseSqlite(_conn).Options;
        return new BankDbContext(options);
    }

    private static void Seed(BankDbContext db, bool holdSession = false)
    {
        var user = new User
        {
            Username = "ash",
            PasswordHash = "hash",
            IsAdmin = true,
            CreatedAt = DateTimeOffset.Parse("2026-01-02T00:00:00Z"),
        };
        var box = new BankBox { User = user, Index = 0, Name = "Box 1" };
        db.Users.Add(user);
        db.BankBoxes.Add(box);
        db.BankPokemon.Add(new BankPokemon
        {
            BankBox = box,
            Slot = 0,
            PkmData = [1, 2, 3, 4],
            Format = 7,
            Species = 25,
            Nickname = "Pika",
            OriginalTrainer = "Ash",
            OriginVersion = "US",
            Level = 50,
            IsLegal = true,
            LegalityReport = "ok",
            Sha256 = "abcd",
            DepositedAt = DateTimeOffset.Parse("2026-01-03T00:00:00Z"),
            Committed = false,
            HeldBySessionId = holdSession ? Guid.Parse("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee") : null,
        });
    }

    private static byte[] Gzip(byte[] json)
    {
        using var output = new MemoryStream();
        using (var gzip = new GZipStream(output, CompressionLevel.SmallestSize, leaveOpen: true))
            gzip.Write(json);
        return output.ToArray();
    }
}
