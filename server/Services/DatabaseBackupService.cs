using System.IO.Compression;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using Microsoft.EntityFrameworkCore;
using Pockettransfer.Server.Data;

namespace Pockettransfer.Server.Services;

public sealed class DatabaseBackupService(BankDbContext db, DataPaths paths)
{
    public const int SnapshotVersion = 1;
    public const int MaxBackupBytes = 200 * 1024 * 1024;
    public const int MaxUncompressedBytes = 500 * 1024 * 1024;
    public const string FileExtension = ".ptbak";
    public const string ConfirmPhrase = "RESTORE";

    private static readonly SemaphoreSlim Gate = new(1, 1);
    private static readonly Regex SafeName = new(@"^[A-Za-z0-9._-]+\.ptbak$", RegexOptions.CultureInvariant);
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        PropertyNameCaseInsensitive = true,
        WriteIndented = false,
    };

    public string EngineName => db.Database.IsNpgsql() ? "PostgreSQL" : "SQLite";

    public Task<bool> UserExistsAsync(int id, CancellationToken ct) =>
        db.Users.AsNoTracking().AnyAsync(u => u.Id == id, ct);

    public async Task<BackupFile> ExportAsync(CancellationToken ct)
    {
        await Gate.WaitAsync(ct);
        try
        {
            var snapshot = await CaptureAsync(ct);
            var bytes = Compress(Serialize(snapshot));
            var name = $"pockettransfer-{DateTime.UtcNow:yyyyMMdd-HHmmss}{FileExtension}";
            return new BackupFile(name, bytes);
        }
        finally
        {
            Gate.Release();
        }
    }

    public async Task<BackupInfo> CreateBackupAsync(CancellationToken ct)
    {
        var file = await ExportAsync(ct);
        EnsureBackupDir();
        var dest = Path.Combine(paths.Backups, file.Name);
        await WriteAtomicallyAsync(dest, file.Bytes, ct);
        return ToInfo(new FileInfo(dest));
    }

    public IReadOnlyList<BackupInfo> ListBackups()
    {
        EnsureBackupDir();
        return Directory.EnumerateFiles(paths.Backups, "*" + FileExtension)
            .Select(f => new FileInfo(f))
            .OrderByDescending(f => f.LastWriteTimeUtc)
            .Select(ToInfo)
            .ToList();
    }

    public async Task<BackupFile> ReadBackupAsync(string name, CancellationToken ct)
    {
        var path = ResolveBackupPath(name);
        var bytes = await File.ReadAllBytesAsync(path, ct);
        return new BackupFile(Path.GetFileName(path), bytes);
    }

    public void DeleteBackup(string name)
    {
        File.Delete(ResolveBackupPath(name));
    }

    public async Task RestoreFromBackupAsync(string name, string confirm, CancellationToken ct)
    {
        var path = ResolveBackupPath(name);
        var bytes = await File.ReadAllBytesAsync(path, ct);
        await RestoreAsync(bytes, confirm, makeSafetyCopy: true, ct);
    }

    public async Task ImportAsync(Stream upload, string confirm, CancellationToken ct)
    {
        if (upload.CanSeek)
        {
            if (upload.Length > MaxBackupBytes)
                throw new InvalidOperationException("Backup file is too large.");
        }

        using var ms = new MemoryStream();
        await upload.CopyToAsync(ms, ct);
        if (ms.Length == 0)
            throw new InvalidOperationException("Choose a backup file.");
        if (ms.Length > MaxBackupBytes)
            throw new InvalidOperationException("Backup file is too large.");
        await RestoreAsync(ms.ToArray(), confirm, makeSafetyCopy: true, ct);
    }

    public static string FormatSize(long bytes) =>
        bytes switch
        {
            < 1024 => $"{bytes} B",
            < 1024 * 1024 => $"{bytes / 1024.0:0.#} KB",
            _ => $"{bytes / (1024.0 * 1024.0):0.#} MB",
        };

    public static bool IsSafeBackupName(string name) =>
        !string.IsNullOrWhiteSpace(name)
        && name.Length is > 0 and <= 128
        && !name.Contains("..", StringComparison.Ordinal)
        && SafeName.IsMatch(name);

    internal static byte[] Compress(byte[] json)
    {
        using var output = new MemoryStream();
        using (var gzip = new GZipStream(output, CompressionLevel.SmallestSize, leaveOpen: true))
            gzip.Write(json);
        return output.ToArray();
    }

    internal static DatabaseSnapshot Parse(byte[] bytes)
    {
        var json = Decode(bytes);
        DatabaseSnapshot snapshot;
        try
        {
            snapshot = JsonSerializer.Deserialize<DatabaseSnapshot>(json, JsonOptions)
                       ?? throw new InvalidOperationException("Backup file is empty.");
        }
        catch (JsonException)
        {
            throw new InvalidOperationException("Backup file is not valid JSON.");
        }

        Validate(snapshot);
        return snapshot;
    }

    private async Task RestoreAsync(byte[] bytes, string confirm, bool makeSafetyCopy, CancellationToken ct)
    {
        if (!string.Equals(confirm.Trim(), ConfirmPhrase, StringComparison.Ordinal))
            throw new InvalidOperationException($"Type {ConfirmPhrase} to confirm.");

        var snapshot = Parse(bytes);

        await Gate.WaitAsync(ct);
        try
        {
            if (makeSafetyCopy)
                await WriteSafetyCopyUnlockedAsync(ct);

            await using var tx = await db.Database.BeginTransactionAsync(ct);
            await ReplaceAllAsync(snapshot, ct);
            await tx.CommitAsync(ct);
            db.ChangeTracker.Clear();
        }
        finally
        {
            Gate.Release();
        }
    }

    private async Task<DatabaseSnapshot> CaptureAsync(CancellationToken ct)
    {
        var users = await db.Users.AsNoTracking().OrderBy(u => u.Id).ToListAsync(ct);
        var devices = await db.Devices.AsNoTracking().OrderBy(d => d.Id).ToListAsync(ct);
        var codes = await db.PairingCodes.AsNoTracking().OrderBy(c => c.Id).ToListAsync(ct);
        var boxes = await db.BankBoxes.AsNoTracking().OrderBy(b => b.Id).ToListAsync(ct);
        var pokemon = await db.BankPokemon.AsNoTracking().OrderBy(p => p.Id).ToListAsync(ct);

        return new DatabaseSnapshot
        {
            Version = SnapshotVersion,
            ExportedAt = DateTimeOffset.UtcNow,
            Engine = EngineName,
            Users = users.ConvertAll(u => new UserSnap
            {
                Id = u.Id,
                Username = u.Username,
                Email = u.Email,
                PasswordHash = u.PasswordHash,
                IsAdmin = u.IsAdmin,
                CreatedAt = u.CreatedAt,
            }),
            Devices = devices.ConvertAll(d => new DeviceSnap
            {
                Id = d.Id,
                UserId = d.UserId,
                Name = d.Name,
                Platform = d.Platform,
                TokenHash = d.TokenHash,
                CreatedAt = d.CreatedAt,
                LastUsedAt = d.LastUsedAt,
            }),
            PairingCodes = codes.ConvertAll(c => new PairingSnap
            {
                Id = c.Id,
                UserId = c.UserId,
                Code = c.Code,
                ExpiresAt = c.ExpiresAt,
            }),
            Boxes = boxes.ConvertAll(b => new BoxSnap
            {
                Id = b.Id,
                UserId = b.UserId,
                Index = b.Index,
                Name = b.Name,
            }),
            Pokemon = pokemon.ConvertAll(p => new PokemonSnap
            {
                Id = p.Id,
                BankBoxId = p.BankBoxId,
                Slot = p.Slot,
                PkmData = p.PkmData,
                Format = p.Format,
                EntityContext = p.EntityContext,
                Species = p.Species,
                Form = p.Form,
                Nickname = p.Nickname,
                OriginalTrainer = p.OriginalTrainer,
                OriginVersion = p.OriginVersion,
                IsShiny = p.IsShiny,
                Level = p.Level,
                IsLegal = p.IsLegal,
                LegalityReport = p.LegalityReport,
                Sha256 = p.Sha256,
                DepositedAt = p.DepositedAt,
                Committed = p.Committed,
            }),
        };
    }

    private async Task ReplaceAllAsync(DatabaseSnapshot snapshot, CancellationToken ct)
    {
        await db.BankPokemon.ExecuteDeleteAsync(ct);
        await db.BankBoxes.ExecuteDeleteAsync(ct);
        await db.PairingCodes.ExecuteDeleteAsync(ct);
        await db.Devices.ExecuteDeleteAsync(ct);
        await db.Users.ExecuteDeleteAsync(ct);
        db.ChangeTracker.Clear();

        db.Users.AddRange(snapshot.Users.Select(u => new User
        {
            Id = u.Id,
            Username = u.Username,
            Email = u.Email,
            PasswordHash = u.PasswordHash,
            IsAdmin = u.IsAdmin,
            CreatedAt = u.CreatedAt,
        }));
        db.Devices.AddRange(snapshot.Devices.Select(d => new Device
        {
            Id = d.Id,
            UserId = d.UserId,
            Name = d.Name,
            Platform = d.Platform,
            TokenHash = d.TokenHash,
            CreatedAt = d.CreatedAt,
            LastUsedAt = d.LastUsedAt,
        }));
        db.PairingCodes.AddRange(snapshot.PairingCodes.Select(c => new PairingCode
        {
            Id = c.Id,
            UserId = c.UserId,
            Code = c.Code,
            ExpiresAt = c.ExpiresAt,
        }));
        db.BankBoxes.AddRange(snapshot.Boxes.Select(b => new BankBox
        {
            Id = b.Id,
            UserId = b.UserId,
            Index = b.Index,
            Name = b.Name,
        }));
        db.BankPokemon.AddRange(snapshot.Pokemon.Select(p => new BankPokemon
        {
            Id = p.Id,
            BankBoxId = p.BankBoxId,
            Slot = p.Slot,
            PkmData = p.PkmData,
            Format = p.Format,
            EntityContext = p.EntityContext,
            Species = p.Species,
            Form = p.Form,
            Nickname = p.Nickname,
            OriginalTrainer = p.OriginalTrainer,
            OriginVersion = p.OriginVersion,
            IsShiny = p.IsShiny,
            Level = p.Level,
            IsLegal = p.IsLegal,
            LegalityReport = p.LegalityReport,
            Sha256 = p.Sha256,
            DepositedAt = p.DepositedAt,
            Committed = true,
            HeldBySessionId = null,
        }));

        await db.SaveChangesAsync(ct);
        await ResetIdentityAsync(ct);
    }

    private async Task ResetIdentityAsync(CancellationToken ct)
    {
        // Table names are a fixed allow-list, not user input.
#pragma warning disable EF1002
        if (db.Database.IsNpgsql())
        {
            foreach (var table in new[] { "Users", "Devices", "PairingCodes", "BankBoxes", "BankPokemon" })
            {
                var sql =
                    $"""
                    SELECT setval(
                        pg_get_serial_sequence('"{table}"', 'Id'),
                        COALESCE((SELECT MAX("Id") FROM "{table}"), 1),
                        (SELECT MAX("Id") FROM "{table}") IS NOT NULL);
                    """;
                await db.Database.ExecuteSqlRawAsync(sql, ct);
            }

            return;
        }

        if (!db.Database.IsSqlite())
            return;

        try
        {
            foreach (var table in new[] { "Users", "Devices", "PairingCodes", "BankBoxes", "BankPokemon" })
            {
                await db.Database.ExecuteSqlRawAsync(
                    $"""
                    INSERT INTO sqlite_sequence(name, seq)
                    SELECT '{table}', COALESCE((SELECT MAX(Id) FROM {table}), 0)
                    WHERE NOT EXISTS (SELECT 1 FROM sqlite_sequence WHERE name = '{table}');
                    """, ct);
                await db.Database.ExecuteSqlRawAsync(
                    $"""
                    UPDATE sqlite_sequence
                    SET seq = COALESCE((SELECT MAX(Id) FROM {table}), 0)
                    WHERE name = '{table}';
                    """, ct);
            }
        }
        catch (Exception)
        {
            // sqlite_sequence is missing when the tables are not AUTOINCREMENT.
        }
#pragma warning restore EF1002
    }

    private async Task WriteSafetyCopyUnlockedAsync(CancellationToken ct)
    {
        EnsureBackupDir();
        var snapshot = await CaptureAsync(ct);
        var bytes = Compress(Serialize(snapshot));
        var dest = Path.Combine(paths.Backups, $"pre-restore-{DateTime.UtcNow:yyyyMMdd-HHmmss}{FileExtension}");
        await WriteAtomicallyAsync(dest, bytes, ct);
        PrunePreRestore();
    }

    private void PrunePreRestore()
    {
        foreach (var extra in Directory.EnumerateFiles(paths.Backups, "pre-restore-*" + FileExtension)
                     .Select(f => new FileInfo(f))
                     .OrderByDescending(f => f.LastWriteTimeUtc)
                     .Skip(10))
        {
            extra.Delete();
        }
    }

    private void EnsureBackupDir() => Directory.CreateDirectory(paths.Backups);

    private string ResolveBackupPath(string name)
    {
        if (!IsSafeBackupName(name))
            throw new InvalidOperationException("Invalid backup name.");
        var full = Path.GetFullPath(Path.Combine(paths.Backups, name));
        var root = Path.GetFullPath(paths.Backups);
        if (!full.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.Ordinal)
            && !string.Equals(full, root, StringComparison.Ordinal))
            throw new InvalidOperationException("Invalid backup name.");
        if (!File.Exists(full))
            throw new InvalidOperationException("Backup not found.");
        return full;
    }

    private static byte[] Serialize(DatabaseSnapshot snapshot) =>
        JsonSerializer.SerializeToUtf8Bytes(snapshot, JsonOptions);

    private static byte[] Decode(byte[] bytes)
    {
        if (bytes.Length >= 1 && bytes[0] == (byte)'{')
        {
            if (bytes.Length > MaxUncompressedBytes)
                throw new InvalidOperationException("Backup file is too large.");
            return bytes;
        }

        if (bytes.Length < 2 || bytes[0] != 0x1F || bytes[1] != 0x8B)
            throw new InvalidOperationException("Backup file is not a Pocket Transfer backup.");

        using var input = new MemoryStream(bytes);
        using var gzip = new GZipStream(input, CompressionMode.Decompress);
        using var output = new MemoryStream();
        var buffer = new byte[81920];
        long total = 0;
        int read;
        while ((read = gzip.Read(buffer, 0, buffer.Length)) > 0)
        {
            total += read;
            if (total > MaxUncompressedBytes)
                throw new InvalidOperationException("Backup is too large after decompression.");
            output.Write(buffer, 0, read);
        }

        return output.ToArray();
    }

    private static void Validate(DatabaseSnapshot snapshot)
    {
        if (snapshot.Version != SnapshotVersion)
            throw new InvalidOperationException($"Unsupported backup version {snapshot.Version}.");
        if (snapshot.Users.Count == 0)
            throw new InvalidOperationException("Backup has no users.");
        if (!snapshot.Users.Any(u => u.IsAdmin))
            throw new InvalidOperationException("Backup has no admin user.");
        if (snapshot.Users.Select(u => u.Id).Distinct().Count() != snapshot.Users.Count)
            throw new InvalidOperationException("Backup has duplicate user ids.");
        if (snapshot.Users.Select(u => u.Username).Distinct(StringComparer.OrdinalIgnoreCase).Count() != snapshot.Users.Count)
            throw new InvalidOperationException("Backup has duplicate usernames.");
        foreach (var user in snapshot.Users)
        {
            if (user.Id <= 0 || string.IsNullOrWhiteSpace(user.Username) || string.IsNullOrWhiteSpace(user.PasswordHash))
                throw new InvalidOperationException("Backup has an invalid user.");
        }

        var userIds = snapshot.Users.Select(u => u.Id).ToHashSet();
        var boxIds = snapshot.Boxes.Select(b => b.Id).ToHashSet();
        if (snapshot.Devices.Any(d => !userIds.Contains(d.UserId)))
            throw new InvalidOperationException("Backup has a device with no user.");
        if (snapshot.PairingCodes.Any(c => !userIds.Contains(c.UserId)))
            throw new InvalidOperationException("Backup has a pairing code with no user.");
        if (snapshot.Boxes.Any(b => !userIds.Contains(b.UserId)))
            throw new InvalidOperationException("Backup has a box with no user.");
        if (snapshot.Pokemon.Any(p => !boxIds.Contains(p.BankBoxId)))
            throw new InvalidOperationException("Backup has a Pokémon with no box.");
        if (snapshot.Boxes.Select(b => (b.UserId, b.Index)).Distinct().Count() != snapshot.Boxes.Count)
            throw new InvalidOperationException("Backup has duplicate box indexes.");
        if (snapshot.Pokemon.Select(p => (p.BankBoxId, p.Slot)).Distinct().Count() != snapshot.Pokemon.Count)
            throw new InvalidOperationException("Backup has duplicate box slots.");
        foreach (var p in snapshot.Pokemon)
        {
            if (p.PkmData is not { Length: > 0 } || string.IsNullOrWhiteSpace(p.Sha256))
                throw new InvalidOperationException("Backup has an invalid Pokémon.");
        }
    }

    private static async Task WriteAtomicallyAsync(string dest, byte[] bytes, CancellationToken ct)
    {
        var tmp = dest + ".tmp";
        await File.WriteAllBytesAsync(tmp, bytes, ct);
        File.Move(tmp, dest, overwrite: true);
    }

    private static BackupInfo ToInfo(FileInfo file) =>
        new(file.Name, file.Length, new DateTimeOffset(file.LastWriteTimeUtc, TimeSpan.Zero));
}

public sealed record BackupFile(string Name, byte[] Bytes);

public sealed record BackupInfo(string Name, long SizeBytes, DateTimeOffset WrittenAt);

public sealed class DatabaseSnapshot
{
    public int Version { get; set; }
    public DateTimeOffset ExportedAt { get; set; }
    public string Engine { get; set; } = "";
    public List<UserSnap> Users { get; set; } = [];
    public List<DeviceSnap> Devices { get; set; } = [];
    public List<PairingSnap> PairingCodes { get; set; } = [];
    public List<BoxSnap> Boxes { get; set; } = [];
    public List<PokemonSnap> Pokemon { get; set; } = [];
}

public sealed class UserSnap
{
    public int Id { get; set; }
    public string Username { get; set; } = "";
    public string? Email { get; set; }
    public string PasswordHash { get; set; } = "";
    public bool IsAdmin { get; set; }
    public DateTimeOffset CreatedAt { get; set; }
}

public sealed class DeviceSnap
{
    public int Id { get; set; }
    public int UserId { get; set; }
    public string Name { get; set; } = "";
    public string Platform { get; set; } = "";
    public string TokenHash { get; set; } = "";
    public DateTimeOffset CreatedAt { get; set; }
    public DateTimeOffset? LastUsedAt { get; set; }
}

public sealed class PairingSnap
{
    public int Id { get; set; }
    public int UserId { get; set; }
    public string Code { get; set; } = "";
    public DateTimeOffset ExpiresAt { get; set; }
}

public sealed class BoxSnap
{
    public int Id { get; set; }
    public int UserId { get; set; }
    public int Index { get; set; }
    public string Name { get; set; } = "";
}

public sealed class PokemonSnap
{
    public int Id { get; set; }
    public int BankBoxId { get; set; }
    public int Slot { get; set; }
    public byte[] PkmData { get; set; } = [];
    public int Format { get; set; }
    public int EntityContext { get; set; }
    public int Species { get; set; }
    public int Form { get; set; }
    public string Nickname { get; set; } = "";
    public string OriginalTrainer { get; set; } = "";
    public string OriginVersion { get; set; } = "";
    public bool IsShiny { get; set; }
    public int Level { get; set; }
    public bool IsLegal { get; set; }
    public string LegalityReport { get; set; } = "";
    public string Sha256 { get; set; } = "";
    public DateTimeOffset DepositedAt { get; set; }
    public bool Committed { get; set; } = true;
}
