using Microsoft.EntityFrameworkCore;
using Pockettransfer.Server.Data;
using Pockettransfer.Server.Options;

namespace Pockettransfer.Server.Services;

public static class DatabaseBootstrap
{
    public static async Task InitializeAsync(BankDbContext db, BankOptions? options = null, CancellationToken ct = default)
    {
        await db.Database.EnsureCreatedAsync(ct);
        await PatchUsersTableAsync(db, ct);
        await BackfillUsernamesAsync(db, ct);
        await EnsureUsernameIndexAsync(db, ct);
        await SeedAdminsAsync(db, options, ct);
    }

    private static async Task PatchUsersTableAsync(BankDbContext db, CancellationToken ct)
    {
        if (db.Database.IsNpgsql())
        {
            await db.Database.ExecuteSqlRawAsync(
                """
                ALTER TABLE "Users" ADD COLUMN IF NOT EXISTS "Username" character varying(32) NOT NULL DEFAULT '';
                ALTER TABLE "Users" ADD COLUMN IF NOT EXISTS "IsAdmin" boolean NOT NULL DEFAULT FALSE;
                DO $patch$
                BEGIN
                    IF EXISTS (
                        SELECT 1 FROM information_schema.columns
                        WHERE table_schema = current_schema()
                          AND table_name = 'Users'
                          AND column_name = 'Email'
                    ) THEN
                        ALTER TABLE "Users" ALTER COLUMN "Email" DROP NOT NULL;
                    END IF;
                END $patch$;
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

            try
            {
                await db.Database.ExecuteSqlRawAsync(
                    "ALTER TABLE Users ADD COLUMN IsAdmin INTEGER NOT NULL DEFAULT 0;", ct);
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

    private static async Task SeedAdminsAsync(BankDbContext db, BankOptions? options, CancellationToken ct)
    {
        var configured = AccountService.ParseAdminUsernames(options?.AdminUsernames);
        if (configured.Count > 0)
        {
            var names = configured.ToList();
            var matches = await db.Users.Where(u => names.Contains(u.Username)).ToListAsync(ct);
            foreach (var user in matches)
                user.IsAdmin = true;
            await db.SaveChangesAsync(ct);
        }

        if (await db.Users.AnyAsync(u => u.IsAdmin, ct))
            return;

        var oldest = await db.Users.OrderBy(u => u.Id).FirstOrDefaultAsync(ct);
        if (oldest is null)
            return;

        oldest.IsAdmin = true;
        await db.SaveChangesAsync(ct);
    }
}
