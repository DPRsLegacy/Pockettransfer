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
    IOptions<BankOptions> options,
    LoginThrottle throttle)
{
    private static readonly Regex UsernamePattern = new(@"^[a-z0-9_]{3,32}$", RegexOptions.Compiled);

    public async Task<User> RegisterAsync(string username, string password, CancellationToken ct)
    {
        username = NormalizeUsername(username);
        if (!UsernamePattern.IsMatch(username))
            throw new InvalidOperationException("Username must be 3–32 characters: lowercase letters, numbers, underscore.");
        if (password.Length < 8)
            throw new InvalidOperationException("Password must be at least 8 characters.");
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
        user.IsAdmin = AccountService.ParseAdminUsernames(options.Value.AdminUsernames).Contains(username)
                       || !await db.Users.AnyAsync(u => u.IsAdmin, ct);
        db.Users.Add(user);
        await db.SaveChangesAsync(ct);
        await EnsureBoxesAsync(user.Id, ct);
        return user;
    }

    public async Task<AuthResult> AuthenticateAsync(string login, string password, string clientIp, CancellationToken ct)
    {
        if (string.IsNullOrWhiteSpace(clientIp))
            clientIp = "unknown";

        if (!throttle.Allow(clientIp, login))
            return AuthResult.Limited();

        var user = await FindByLoginAsync(login, ct);
        if (user is null)
        {
            throttle.RecordFailure(clientIp, login);
            return AuthResult.Invalid();
        }

        var result = hasher.VerifyHashedPassword(user, user.PasswordHash, password);
        if (result == PasswordVerificationResult.Failed)
        {
            throttle.RecordFailure(clientIp, login);
            return AuthResult.Invalid();
        }

        if (result == PasswordVerificationResult.SuccessRehashNeeded)
        {
            user.PasswordHash = hasher.HashPassword(user, password);
            await db.SaveChangesAsync(ct);
        }

        throttle.RecordSuccess(clientIp, login);
        return AuthResult.Ok(user);
    }

    public async Task<User?> FindByLoginAsync(string login, CancellationToken ct)
    {
        login = login.Trim();
        if (login.Length == 0)
            return null;

        var username = NormalizeUsername(login);
        var user = await db.Users.FirstOrDefaultAsync(u => u.Username == username, ct);
        if (user is not null)
            return user;

        if (!login.Contains('@'))
            return null;

        var email = login.ToLowerInvariant();
        return await db.Users.FirstOrDefaultAsync(u => u.Email != null && u.Email.ToLower() == email, ct);
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

    /// <summary>
    /// First-connect enroll: consume the newest live pairing code, or (if this
    /// instance has exactly one account) issue a token for that user.
    /// </summary>
    public async Task<(User User, string Token)?> EnrollConsoleAsync(string name, string platform, CancellationToken ct)
    {
        var now = DateTimeOffset.UtcNow;
        var expired = db.PairingCodes.Where(c => c.ExpiresAt < now);
        db.PairingCodes.RemoveRange(expired);
        await db.SaveChangesAsync(ct);

        var pending = await db.PairingCodes.Include(c => c.User)
            .OrderByDescending(c => c.Id)
            .FirstOrDefaultAsync(ct);
        if (pending is not null)
            return await PairDeviceAsync(pending.Code, name, platform, ct);

        if (await db.Users.CountAsync(ct) != 1)
            return null;

        var user = await db.Users.SingleAsync(ct);
        var token = await IssueDeviceTokenAsync(user, name, platform, ct);
        return (user, token);
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

    public async Task<bool> IsAdminAsync(ClaimsPrincipal principal, CancellationToken ct)
    {
        var raw = principal.FindFirstValue(ClaimTypes.NameIdentifier);
        if (raw is null || !int.TryParse(raw, out var userId))
            return false;
        return await db.Users.AnyAsync(u => u.Id == userId && u.IsAdmin, ct);
    }

    public static HashSet<string> ParseAdminUsernames(string? raw)
    {
        if (string.IsNullOrWhiteSpace(raw))
            return [];
        return raw.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .Select(NormalizeUsername)
            .Where(s => s.Length > 0)
            .ToHashSet(StringComparer.Ordinal);
    }

    public static bool IsValidUsername(string username) => UsernamePattern.IsMatch(NormalizeUsername(username));
}

public readonly record struct AuthResult(User? User, bool RateLimited)
{
    public bool Succeeded => User is not null;
    public static AuthResult Ok(User user) => new(user, false);
    public static AuthResult Invalid() => new(null, false);
    public static AuthResult Limited() => new(null, true);
}
