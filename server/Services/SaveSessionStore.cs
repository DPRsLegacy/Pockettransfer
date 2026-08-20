using PKHeX.Core;

namespace Pockettransfer.Server.Services;

public sealed class SaveSession
{
    public required Guid Id { get; init; }
    public required int UserId { get; init; }
    public required SaveFile Save { get; set; }
    public required string FileName { get; init; }
    public DateTimeOffset ExpiresAt { get; set; }
}

public sealed class SaveSessionStore
{
    private readonly Dictionary<Guid, SaveSession> _sessions = new();
    private readonly Lock _gate = new();

    public SaveSession Create(int userId, SaveFile save, string fileName, TimeSpan lifetime)
    {
        var session = new SaveSession
        {
            Id = Guid.NewGuid(),
            UserId = userId,
            Save = save,
            FileName = fileName,
            ExpiresAt = DateTimeOffset.UtcNow + lifetime,
        };
        lock (_gate)
        {
            Sweep();
            _sessions[session.Id] = session;
        }

        return session;
    }

    public bool Exists(Guid id)
    {
        lock (_gate)
        {
            Sweep();
            return _sessions.ContainsKey(id);
        }
    }

    public HashSet<Guid> ActiveIds()
    {
        lock (_gate)
        {
            Sweep();
            return _sessions.Keys.ToHashSet();
        }
    }

    public SaveSession? Get(Guid id, int userId)
    {
        lock (_gate)
        {
            Sweep();
            if (!_sessions.TryGetValue(id, out var session) || session.UserId != userId)
                return null;
            return session;
        }
    }

    public void Touch(SaveSession session, TimeSpan lifetime)
    {
        session.ExpiresAt = DateTimeOffset.UtcNow + lifetime;
    }

    public void Remove(Guid id)
    {
        lock (_gate)
            _sessions.Remove(id);
    }

    private void Sweep()
    {
        var now = DateTimeOffset.UtcNow;
        foreach (var id in _sessions.Where(s => s.Value.ExpiresAt < now).Select(s => s.Key).ToList())
            _sessions.Remove(id);
    }
}
