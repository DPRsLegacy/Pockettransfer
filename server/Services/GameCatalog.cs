using System.Text.Json;
using System.Text.Json.Serialization;

namespace Pockettransfer.Server.Services;

public sealed class GameInfoRecord
{
    public required string Id { get; init; }
    public required string Name { get; init; }
    public required string Platform { get; init; }
    public int Generation { get; init; }
    public int Format { get; init; }
    public required string PkhexVersion { get; init; }
    public required string TitleId { get; init; }
    public required string[] SaveFiles { get; init; }
    public required string Archive { get; init; }
}

public sealed class GameCatalog
{
    public IReadOnlyList<GameInfoRecord> Games { get; }

    public GameCatalog(IWebHostEnvironment env)
    {
        var paths = new[]
        {
            Path.Combine(env.ContentRootPath, "Data", "games.json"),
            Path.Combine(env.ContentRootPath, "..", "shared", "games.json"),
        };
        var path = paths.First(File.Exists);
        using var stream = File.OpenRead(path);
        var root = JsonSerializer.Deserialize<GamesFile>(stream, new JsonSerializerOptions
        {
            PropertyNameCaseInsensitive = true,
        }) ?? throw new InvalidOperationException("games.json is empty.");
        Games = root.Games;
    }

    public GameInfoRecord? FindByTitleId(string titleId) =>
        Games.FirstOrDefault(g => g.TitleId.Equals(titleId, StringComparison.OrdinalIgnoreCase));

    public GameInfoRecord? FindById(string id) =>
        Games.FirstOrDefault(g => g.Id.Equals(id, StringComparison.OrdinalIgnoreCase));

    private sealed class GamesFile
    {
        [JsonPropertyName("games")]
        public List<GameInfoRecord> Games { get; set; } = [];
    }
}
