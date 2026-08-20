using System.Security.Claims;
using System.Security.Cryptography;
using Microsoft.AspNetCore.Identity;
using Microsoft.EntityFrameworkCore;
using Pockettransfer.Server.Data;

namespace Pockettransfer.Server.Services;

public sealed class AdminService(
    BankDbContext db,
    IPasswordHasher<User> hasher,
    AccountService accounts)
{
    public const int PageSize = 40;

    public async Task<AdminStats> GetStatsAsync(CancellationToken ct)
    {
        var now = DateTimeOffset.UtcNow;
        return new AdminStats(
            await db.Users.CountAsync(ct),
            await db.Users.CountAsync(u => u.IsAdmin, ct),
            await db.BankPokemon.CountAsync(ct),
            await db.BankPokemon.CountAsync(p => !p.IsLegal, ct),
            await db.Devices.CountAsync(ct),
            await db.PairingCodes.CountAsync(c => c.ExpiresAt >= now, ct),
            await db.PairingCodes.CountAsync(c => c.ExpiresAt < now, ct));
    }

    public async Task<IReadOnlyList<User>> RecentUsersAsync(int take, CancellationToken ct) =>
        await db.Users.AsNoTracking()
            .OrderByDescending(u => u.CreatedAt)
            .Take(take)
            .ToListAsync(ct);

    public async Task<(IReadOnlyList<UserRow> Users, int Total)> ListUsersAsync(
        string? query, int page, CancellationToken ct)
    {
        page = Math.Max(1, page);
        var q = db.Users.AsNoTracking();
        if (!string.IsNullOrWhiteSpace(query))
        {
            var needle = query.Trim().ToLowerInvariant();
            q = q.Where(u => u.Username.Contains(needle) || (u.Email != null && u.Email.ToLower().Contains(needle)));
        }

        var total = await q.CountAsync(ct);
        var users = await q
            .OrderBy(u => u.Username)
            .Skip((page - 1) * PageSize)
            .Take(PageSize)
            .Select(u => new UserRow(
                u.Id,
                u.Username,
                u.Email,
                u.IsAdmin,
                u.CreatedAt,
                u.Devices.Count,
                u.Boxes.SelectMany(b => b.Pokemon).Count()))
            .ToListAsync(ct);
        return (users, total);
    }

    public async Task<UserDetail?> GetUserAsync(int id, CancellationToken ct)
    {
        var user = await db.Users.AsNoTracking().FirstOrDefaultAsync(u => u.Id == id, ct);
        if (user is null)
            return null;

        var devices = await db.Devices.AsNoTracking()
            .Where(d => d.UserId == id)
            .OrderByDescending(d => d.CreatedAt)
            .ToListAsync(ct);
        var boxes = await db.BankBoxes.AsNoTracking()
            .Where(b => b.UserId == id)
            .OrderBy(b => b.Index)
            .Select(b => new BoxRow(b.Id, b.Index, b.Name, b.Pokemon.Count))
            .ToListAsync(ct);

        return new UserDetail(user, devices, boxes, boxes.Sum(b => b.PokemonCount));
    }

    public async Task ResetPasswordAsync(int userId, string password, CancellationToken ct)
    {
        if (password.Length < 8)
            throw new InvalidOperationException("Password must be at least 8 characters.");
        var user = await RequireUserAsync(userId, ct);
        user.PasswordHash = hasher.HashPassword(user, password);
        await db.SaveChangesAsync(ct);
    }

    public static string GeneratePassword() =>
        RandomNumberGenerator.GetString("ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789", 12);

    public async Task RenameUserAsync(int userId, string username, CancellationToken ct)
    {
        username = AccountService.NormalizeUsername(username);
        if (!AccountService.IsValidUsername(username))
            throw new InvalidOperationException("Username must be 3–32 characters: lowercase letters, numbers, underscore.");
        if (await db.Users.AnyAsync(u => u.Username == username && u.Id != userId, ct))
            throw new InvalidOperationException("That username is already taken.");
        var user = await RequireUserAsync(userId, ct);
        user.Username = username;
        await db.SaveChangesAsync(ct);
    }

    public async Task SetAdminAsync(int actorId, int userId, bool isAdmin, CancellationToken ct)
    {
        var user = await RequireUserAsync(userId, ct);
        if (user.IsAdmin == isAdmin)
            return;
        if (!isAdmin)
        {
            if (userId == actorId)
                throw new InvalidOperationException("You cannot remove your own admin access.");
            if (await db.Users.CountAsync(u => u.IsAdmin, ct) <= 1)
                throw new InvalidOperationException("Cannot remove the last admin.");
        }

        user.IsAdmin = isAdmin;
        await db.SaveChangesAsync(ct);
    }

    public async Task DeleteUserAsync(int actorId, int userId, string confirmUsername, CancellationToken ct)
    {
        if (userId == actorId)
            throw new InvalidOperationException("You cannot delete your own account here.");
        var user = await RequireUserAsync(userId, ct);
        if (!string.Equals(confirmUsername.Trim(), user.Username, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("Type the username to confirm deletion.");
        if (user.IsAdmin && await db.Users.CountAsync(u => u.IsAdmin, ct) <= 1)
            throw new InvalidOperationException("Cannot delete the last admin.");

        db.PairingCodes.RemoveRange(db.PairingCodes.Where(c => c.UserId == userId));
        db.Devices.RemoveRange(db.Devices.Where(d => d.UserId == userId));
        db.BankPokemon.RemoveRange(db.BankPokemon.Where(p => p.BankBox.UserId == userId));
        db.BankBoxes.RemoveRange(db.BankBoxes.Where(b => b.UserId == userId));
        db.Users.Remove(user);
        await db.SaveChangesAsync(ct);
    }

    public async Task RevokeDeviceAsync(int userId, int deviceId, CancellationToken ct)
    {
        var device = await db.Devices.FirstOrDefaultAsync(d => d.Id == deviceId && d.UserId == userId, ct)
                     ?? throw new InvalidOperationException("Device not found.");
        db.Devices.Remove(device);
        await db.SaveChangesAsync(ct);
    }

    public async Task RenameBoxAsync(int userId, int boxId, string name, CancellationToken ct)
    {
        name = name.Trim();
        if (name.Length is < 1 or > 32)
            throw new InvalidOperationException("Box name must be 1–32 characters.");
        var box = await db.BankBoxes.FirstOrDefaultAsync(b => b.Id == boxId && b.UserId == userId, ct)
                  ?? throw new InvalidOperationException("Box not found.");
        box.Name = name;
        await db.SaveChangesAsync(ct);
    }

    public async Task DeletePokemonAsync(int pokemonId, CancellationToken ct)
    {
        var row = await db.BankPokemon.FirstOrDefaultAsync(p => p.Id == pokemonId, ct)
                  ?? throw new InvalidOperationException("Pokémon not found.");
        db.BankPokemon.Remove(row);
        await db.SaveChangesAsync(ct);
    }

    public async Task<int> SweepExpiredPairingCodesAsync(CancellationToken ct)
    {
        var expired = db.PairingCodes.Where(c => c.ExpiresAt < DateTimeOffset.UtcNow);
        var count = await expired.CountAsync(ct);
        db.PairingCodes.RemoveRange(expired);
        await db.SaveChangesAsync(ct);
        return count;
    }

    public Task EnsureBoxesAsync(int userId, CancellationToken ct) => accounts.EnsureBoxesAsync(userId, ct);

    public static int CurrentUserId(ClaimsPrincipal user)
    {
        var raw = user.FindFirstValue(ClaimTypes.NameIdentifier);
        if (raw is null || !int.TryParse(raw, out var id))
            throw new InvalidOperationException("Not signed in.");
        return id;
    }

    private async Task<User> RequireUserAsync(int userId, CancellationToken ct) =>
        await db.Users.FirstOrDefaultAsync(u => u.Id == userId, ct)
        ?? throw new InvalidOperationException("User not found.");
}

public sealed record AdminStats(
    int Users,
    int Admins,
    int Pokemon,
    int IllegalPokemon,
    int Devices,
    int LivePairingCodes,
    int ExpiredPairingCodes);

public sealed record UserRow(
    int Id,
    string Username,
    string? Email,
    bool IsAdmin,
    DateTimeOffset CreatedAt,
    int DeviceCount,
    int PokemonCount);

public sealed record BoxRow(int Id, int Index, string Name, int PokemonCount);

public sealed record UserDetail(
    User User,
    IReadOnlyList<Device> Devices,
    IReadOnlyList<BoxRow> Boxes,
    int PokemonCount);
