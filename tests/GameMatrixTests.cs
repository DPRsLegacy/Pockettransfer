using PKHeX.Core;
using Xunit;

namespace Pockettransfer.Tests;

public class GameMatrixTests
{
    [Fact]
    public void GamesJson_HasUniqueTitleIdsAndExpectedCount()
    {
        var path = FindGamesJson();
        var json = File.ReadAllText(path);
        Assert.Contains("00040000001B5100", json); // Ultra Moon
        Assert.Contains("0100A3D008C5C000", json); // Scarlet
        Assert.Contains("01008F600124C000", json); // Violet

        using var doc = System.Text.Json.JsonDocument.Parse(json);
        var games = doc.RootElement.GetProperty("games").EnumerateArray().ToList();
        Assert.Equal(31, games.Count);
        Assert.Contains("0004000000171000", json); // Red VC
        Assert.Contains("DS-ADA", json); // Diamond

        var titles = games.Select(g => g.GetProperty("titleId").GetString()).ToList();
        Assert.Equal(titles.Count, titles.Distinct(StringComparer.OrdinalIgnoreCase).Count());

        var platforms = games.Select(g => g.GetProperty("platform").GetString()).ToHashSet();
        Assert.Contains("3ds", platforms);
        Assert.Contains("switch", platforms);

        var archives = games.Select(g => g.GetProperty("archive").GetString()).ToHashSet();
        Assert.Contains("extdata", archives);
        Assert.Contains("twl", archives);

        foreach (var game in games)
        {
            Assert.True(game.GetProperty("format").GetInt32() is >= 1 and <= 9);
            Assert.NotEmpty(game.GetProperty("saveFiles").EnumerateArray().ToList());
        }
    }

    [Theory]
    [InlineData(1, 7, true)]
    [InlineData(2, 7, true)]
    [InlineData(4, 6, true)]
    [InlineData(5, 7, true)]
    [InlineData(7, 8, true)]
    [InlineData(7, 9, true)]
    [InlineData(8, 9, true)]
    [InlineData(9, 7, false)]
    [InlineData(9, 6, false)]
    [InlineData(7, 4, false)]
    public void Convertible_FollowsHomeLikeDirection(int fromFormat, int toFormat, bool expected)
    {
        PKM pk = fromFormat switch
        {
            1 => new PK1 { Species = 25 },
            2 => new PK2 { Species = 25 },
            4 => new PK4 { Species = 25 },
            5 => new PK5 { Species = 25 },
            6 => new PK6 { Species = 25 },
            7 => new PK7 { Species = 25 },
            8 => new PK8 { Species = 25 },
            9 => new PK9 { Species = 25 },
            _ => throw new ArgumentOutOfRangeException(nameof(fromFormat)),
        };
        Assert.Equal(expected, EntityConverter.IsConvertibleToFormat(pk, (byte)toFormat));
    }

    [Fact]
    public void DestSpeciesCap_IsPerSaveType()
    {
        Assert.True(new SAV7USUM().MaxSpeciesID >= 807);
        Assert.True(new SAV8SWSH().MaxSpeciesID >= 890);
        Assert.True(new SAV9SV().MaxSpeciesID >= 1000);
        Assert.True(new SAV9SV().MaxSpeciesID > new SAV7USUM().MaxSpeciesID);
    }

    private static string FindGamesJson()
    {
        var dir = AppContext.BaseDirectory;
        for (var i = 0; i < 8; i++)
        {
            var candidate = Path.Combine(dir, "shared", "games.json");
            if (File.Exists(candidate))
                return candidate;
            candidate = Path.GetFullPath(Path.Combine(dir, "..", "..", "..", "..", "shared", "games.json"));
            if (File.Exists(candidate))
                return candidate;
            dir = Path.GetFullPath(Path.Combine(dir, ".."));
        }

        throw new FileNotFoundException("shared/games.json");
    }
}

public class LiteChecksumTests
{
    [Fact]
    public void KnownSizes_AreAccepted()
    {
        foreach (var size in new[] { 232, 328, 344, 360 })
            Assert.True(LiteCheck.IsKnownStoredSize(size));
        Assert.False(LiteCheck.IsKnownStoredSize(100));
    }

    [Fact]
    public void Checksum_MatchesPkhexOnBlankPk7()
    {
        var pk = new PK7 { Species = 25 };
        pk.RefreshChecksum();
        var data = new byte[pk.SIZE_STORED];
        pk.WriteEncryptedDataStored(data);
        Assert.True(LiteCheck.Validate(data, out var reason), reason);
    }
}

public static class LiteCheck
{
    public static bool IsKnownStoredSize(int size) => size is 232 or 328 or 344 or 360;

    public static bool Validate(byte[] data, out string reason)
    {
        if (!IsKnownStoredSize(data.Length))
        {
            reason = "size";
            return false;
        }

        var copy = (byte[])data.Clone();
        Decrypt(copy);
        ushort stored = (ushort)(copy[6] | (copy[7] << 8));
        ushort calc = 0;
        for (var i = 8; i + 1 < copy.Length; i += 2)
            calc += (ushort)(copy[i] | (copy[i + 1] << 8));
        if (stored != calc)
        {
            reason = $"checksum {stored:X4} != {calc:X4}";
            return false;
        }

        ushort species = (ushort)(copy[8] | (copy[9] << 8));
        if (species is 0 or > 1025)
        {
            reason = "species";
            return false;
        }

        reason = "ok";
        return true;
    }

    private static void Decrypt(byte[] data)
    {
        uint seed = (uint)(data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24));
        for (var i = 8; i + 1 < data.Length; i += 2)
        {
            seed = (0x41C64E6Du * seed) + 0x6073;
            var xor = (ushort)(seed >> 16);
            data[i] ^= (byte)xor;
            data[i + 1] ^= (byte)(xor >> 8);
        }
        Unshuffle(data);
    }

    private static readonly byte[][] Positions =
    [
        [0, 1, 2, 3], [0, 1, 3, 2], [0, 2, 1, 3], [0, 3, 1, 2],
        [0, 2, 3, 1], [0, 3, 2, 1], [1, 0, 2, 3], [1, 0, 3, 2],
        [2, 0, 1, 3], [3, 0, 1, 2], [2, 0, 3, 1], [3, 0, 2, 1],
        [1, 2, 0, 3], [1, 3, 0, 2], [2, 1, 0, 3], [3, 1, 0, 2],
        [2, 3, 0, 1], [3, 2, 0, 1], [1, 2, 3, 0], [1, 3, 2, 0],
        [2, 1, 3, 0], [3, 1, 2, 0], [2, 3, 1, 0], [3, 2, 1, 0],
    ];

    private static void Unshuffle(byte[] data)
    {
        uint ec = (uint)(data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24));
        var index = (int)((ec >> 13) & 31) % 24;
        var block = (data.Length - 8) / 4;
        var orig = data[8..];
        var order = Positions[index];
        var tmp = new byte[data.Length - 8];
        for (var b = 0; b < 4; b++)
            Array.Copy(orig, order[b] * block, tmp, b * block, block);
        Array.Copy(tmp, 0, data, 8, tmp.Length);
    }
}
