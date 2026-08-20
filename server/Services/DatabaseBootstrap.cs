using Microsoft.EntityFrameworkCore;
using Pockettransfer.Server.Data;

namespace Pockettransfer.Server.Services;

public static class DatabaseBootstrap
{
    public static async Task InitializeAsync(BankDbContext db, CancellationToken ct = default)
    {
        await db.Database.EnsureCreatedAsync(ct);
        await PatchUsersTableAsync(db, ct);
        await BackfillUsernamesAsync(db, ct);
        await EnsureUsernameIndexAsync(db, ct);
    }

    private static async Task PatchUsersTableAsync(BankDbContext db, CancellationToken ct)
    {
        if (db.Database.IsNpgsql())
        {
            await db.Database.ExecuteSqlRawAsync(
                """
                ALTER TABLE "Users" ADD COLUMN IF NOT EXISTS "Username" character varying(32) NOT NULL DEFAULT '';
                ALTER TABLE "Users" ALTER COLUMN "Email" DROP NOT NULL;
                DROP INDEX IF EXISTS "IX_Users_Email";
                """, ct);
            return;
        }

        if (db.Database.IsSqlite())
        {
            try
            {
                await db.Database.ExecuteSqlRawAsync(
                    "ALTER TABLE Users ADD COLUMN Username TEXT NOT NULL DEFAULT '';", ct);
            }
            catch
            {
                // column already exists
            }
        }
    }

    private static async Task BackfillUsernamesAsync(BankDbContext db, CancellationToken ct)
    {
        var users = await db.Users.Where(u => u.Username == "").ToListAsync(ct);
        foreach (var user in users)
        {
            user.Username = DeriveUsername(user.Email ?? $"user{user.Id}", user.Id);
            var taken = await db.Users.AnyAsync(u => u.Username == user.Username && u.Id != user.Id, ct);
            if (taken)
                user.Username = $"user{user.Id}";
            await db.SaveChangesAsync(ct);
        }
    }

    private static string DeriveUsername(string seed, int id)
    {
        var baseName = seed.Contains('@') ? seed.Split('@')[0] : seed;
        baseName = AccountService.NormalizeUsername(baseName);
        if (baseName.Length >= 3 && baseName.Length <= 32)
            return baseName;
        return $"user{id}";
    }

    private static async Task EnsureUsernameIndexAsync(BankDbContext db, CancellationToken ct)
    {
        if (!db.Database.IsNpgsql())
            return;

        await db.Database.ExecuteSqlRawAsync(
            """
            CREATE UNIQUE INDEX IF NOT EXISTS "IX_Users_Username" ON "Users" ("Username");
            """, ct);
    }
}
