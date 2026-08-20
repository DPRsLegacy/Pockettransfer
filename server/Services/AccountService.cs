using System.Security.Claims;
using System.Security.Cryptography;
using System.Text.RegularExpressions;
using Microsoft.AspNetCore.Identity;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Options;
using Pockettransfer.Server.Data;
using Pockettransfer.Server.Options;

namespace Pockettransfer.Server.Services;

public sealed class AccountService(
    BankDbContext db,
    IPasswordHasher<User> hasher,
    IOptions<BankOptions> options)
{
    private static readonly Regex UsernamePattern = new(@"^[a-z0-9_]{3,32}$", RegexOptions.Compiled);

    public async Task<User> RegisterAsync(string username, string password, CancellationToken ct)
    {
        username = NormalizeUsername(username);
        if (!UsernamePattern.IsMatch(username))
            throw new InvalidOperationException("Username must be 3–32 characters: lowercase letters, numbers, underscore.");
        if (await db.Users.AnyAsync(u => u.Username == username, ct))
            throw new InvalidOperationException("That username is already taken.");

        var user = new User
        {
            Username = username,
            Email = null,
            PasswordHash = "",
            CreatedAt = DateTimeOffset.UtcNow,
        };
        user.PasswordHash = hasher.HashPassword(user, password);
        db.Users.Add(user);
        await db.SaveChangesAsync(ct);
        await EnsureBoxesAsync(user.Id, ct);
        return user;
    }

    public async Task<User?> AuthenticateAsync(string username, string password, CancellationToken ct)
    {
        username = NormalizeUsername(username);
        var user = await db.Users.FirstOrDefaultAsync(u => u.Username == username, ct);
        if (user is null)
            return null;
        var result = hasher.VerifyHashedPassword(user, user.PasswordHash, password);
        return result is PasswordVerificationResult.Success or PasswordVerificationResult.SuccessRehashNeeded
            ? user
            : null;
    }

    public async Task EnsureBoxesAsync(int userId, CancellationToken ct)
    {
        var existing = await db.BankBoxes.CountAsync(b => b.UserId == userId, ct);
        if (existing >= options.Value.BoxCount)
            return;
        for (var i = existing; i < options.Value.BoxCount; i++)
        {
            db.BankBoxes.Add(new BankBox
            {
                UserId = userId,
                Index = i,
                Name = $"Box {i + 1}",
            });
        }

        await db.SaveChangesAsync(ct);
    }

    public async Task<string> CreatePairingCodeAsync(int userId, CancellationToken ct)
    {
        var expired = db.PairingCodes.Where(c => c.UserId == userId || c.ExpiresAt < DateTimeOffset.UtcNow);
        db.PairingCodes.RemoveRange(expired);

        var code = RandomNumberGenerator.GetString("ABCDEFGHJKLMNPQRSTUVWXYZ23456789", 8);
        db.PairingCodes.Add(new PairingCode
        {
            UserId = userId,
            Code = code,
            ExpiresAt = DateTimeOffset.UtcNow.AddMinutes(options.Value.PairingMinutes),
        });
        await db.SaveChangesAsync(ct);
        return code;
    }

    public async Task<(User User, string Token)?> PairDeviceAsync(string code, string name, string platform, CancellationToken ct)
    {
        code = code.Trim().ToUpperInvariant();
        var row = await db.PairingCodes.Include(c => c.User)
            .FirstOrDefaultAsync(c => c.Code == code, ct);
        if (row is null || row.ExpiresAt < DateTimeOffset.UtcNow)
            return null;

        var token = await IssueDeviceTokenAsync(row.User, name, platform, ct);
        db.PairingCodes.Remove(row);
        await db.SaveChangesAsync(ct);
        return (row.User, token);
    }

    public async Task<string> IssueDeviceTokenAsync(User user, string? name, string? platform, CancellationToken ct)
    {
        var tokenBytes = RandomNumberGenerator.GetBytes(32);
        var token = "pt_" + Convert.ToHexString(tokenBytes).ToLowerInvariant();
        db.Devices.Add(new Device
        {
            UserId = user.Id,
            Name = string.IsNullOrWhiteSpace(name) ? platform ?? "device" : name.Trim(),
            Platform = (platform ?? "unknown").Trim().ToLowerInvariant(),
            TokenHash = HashToken(token),
            CreatedAt = DateTimeOffset.UtcNow,
        });
        await db.SaveChangesAsync(ct);
        return token;
    }

    public async Task<User?> FindByDeviceTokenAsync(string token, CancellationToken ct)
    {
        var hash = HashToken(token);
        var device = await db.Devices.Include(d => d.User).FirstOrDefaultAsync(d => d.TokenHash == hash, ct);
        if (device is null)
            return null;
        device.LastUsedAt = DateTimeOffset.UtcNow;
        await db.SaveChangesAsync(ct);
        return device.User;
    }

    public static string HashToken(string token) =>
        Convert.ToHexString(SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(token)));

    public static string NormalizeUsername(string username) => username.Trim().ToLowerInvariant();

    public static IEnumerable<Claim> BuildClaims(User user) =>
    [
        new(ClaimTypes.NameIdentifier, user.Id.ToString()),
        new(ClaimTypes.Name, user.Username),
    ];
}
