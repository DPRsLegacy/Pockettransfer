using System.Collections.Concurrent;
using System.Net;
using Microsoft.Extensions.Options;
using Pockettransfer.Server.Options;

namespace Pockettransfer.Server.Services;

public sealed class LoginThrottle(IOptions<BankOptions> options)
{
    public const string TooManyAttempts = "Too many login attempts. Try again later.";

    private readonly ConcurrentDictionary<string, AttemptState> _state = new();
    private int _ops;

    public static string ClientKey(HttpContext http)
    {
        var ip = http.Connection.RemoteIpAddress;
        if (ip is null)
            return "unknown";
        if (ip.IsIPv4MappedToIPv6)
            ip = ip.MapToIPv4();
        if (IPAddress.IsLoopback(ip))
            return "loopback";
        return ip.ToString();
    }

    public bool Allow(string ip, string login)
    {
        var now = DateTimeOffset.UtcNow;
        MaybePrune(now);
        var window = Window();

        var ipState = Touch($"i:{ip}", now, window);
        lock (ipState)
        {
            ipState.Count++;
            if (ipState.Count > options.Value.LoginIpAttemptLimit)
                return false;
        }

        var fail = Touch($"f:{ip}:{LoginKey(login)}", now, window);
        lock (fail)
        {
            if (fail.BlockedUntil > now)
                return false;
        }

        return true;
    }

    public void RecordFailure(string ip, string login)
    {
        var now = DateTimeOffset.UtcNow;
        var window = Window();
        var fail = Touch($"f:{ip}:{LoginKey(login)}", now, window);
        lock (fail)
        {
            fail.Count++;
            if (fail.Count >= options.Value.LoginFailureLimit)
                fail.BlockedUntil = now + window;
        }
    }

    public void RecordSuccess(string ip, string login) =>
        _state.TryRemove($"f:{ip}:{LoginKey(login)}", out _);

    private TimeSpan Window() => TimeSpan.FromMinutes(Math.Max(1, options.Value.LoginWindowMinutes));

    private static string LoginKey(string login)
    {
        login = login.Trim();
        if (login.Length == 0)
            return "-";
        return login.Contains('@') ? login.ToLowerInvariant() : AccountService.NormalizeUsername(login);
    }

    private AttemptState Touch(string key, DateTimeOffset now, TimeSpan window)
    {
        var state = _state.GetOrAdd(key, _ => new AttemptState { WindowStart = now });
        lock (state)
        {
            if (state.BlockedUntil > now)
                return state;
            if (now - state.WindowStart >= window)
            {
                state.Count = 0;
                state.WindowStart = now;
                state.BlockedUntil = default;
            }
        }

        return state;
    }

    private void MaybePrune(DateTimeOffset now)
    {
        if (Interlocked.Increment(ref _ops) % 64 != 0)
            return;

        var window = Window();
        foreach (var kv in _state)
        {
            bool stale;
            lock (kv.Value)
                stale = kv.Value.BlockedUntil <= now && now - kv.Value.WindowStart >= window;
            if (stale)
                _state.TryRemove(kv.Key, out _);
        }
    }

    private sealed class AttemptState
    {
        public int Count;
        public DateTimeOffset WindowStart;
        public DateTimeOffset BlockedUntil;
    }
}
