using System.Security.Cryptography;
using PKHeX.Core;

namespace Pockettransfer.Server.Services;

public sealed class LegalitySummary
{
    public bool Valid { get; init; }
    public required string Report { get; init; }
    public IReadOnlyList<string> Issues { get; init; } = [];
}

public sealed class PkHexService
{
    public SaveFile? LoadSave(byte[] data, string fileName = "main")
    {
        return SaveUtil.TryGetSaveFile(data, out var sav, fileName) ? sav : null;
    }

    public byte[] ExportSave(SaveFile sav) => sav.Write().ToArray();

    public PKM? LoadPkm(byte[] data, EntityContext context)
    {
        try
        {
            return EntityFormat.GetFromBytes(data, context);
        }
        catch
        {
            return LoadPkmBySize(data);
        }
    }

    public PKM? LoadPkmBySize(byte[] data) => data.Length switch
    {
        232 => new PK7(data),
        328 => new PK8(data),
        360 => new PA8(data),
        344 => Guess344(data),
        _ => EntityFormat.GetFromBytes(data, EntityContext.None),
    };

    private static PKM Guess344(byte[] data)
    {
        try
        {
            var pk9 = new PK9(data);
            if (pk9.ChecksumValid)
                return pk9;
        }
        catch
        {
            // fall through to BDSP
        }

        return new PB8(data);
    }

    public byte[] ExportStored(PKM pk)
    {
        var buffer = new byte[pk.SIZE_STORED];
        pk.WriteEncryptedDataStored(buffer);
        return buffer;
    }

    public LegalitySummary Analyze(PKM pk, SaveFile? sav = null)
    {
        LegalityAnalysis analysis = sav is null
            ? new LegalityAnalysis(pk)
            : new LegalityAnalysis(pk, sav.Personal, StorageSlotType.Box);

        var issues = new List<string>();
        foreach (var result in analysis.Results)
        {
            var text = result.ToString();
            if (!string.IsNullOrWhiteSpace(text))
                issues.Add(text);
        }

        string report;
        try
        {
            report = analysis.Report();
        }
        catch
        {
            report = issues.Count == 0
                ? (analysis.Valid ? "Legal" : "Illegal")
                : string.Join('\n', issues);
        }

        return new LegalitySummary
        {
            Valid = analysis.Valid,
            Report = report,
            Issues = issues,
        };
    }

    public (PKM? Converted, EntityConverterResult Result) ConvertToSave(PKM pk, SaveFile sav)
    {
        if (pk.GetType() == sav.PKMType)
        {
            sav.AdaptToSaveFile(pk);
            return (pk, EntityConverterResult.None);
        }

        if (!EntityConverter.IsConvertibleToFormat(pk, FormatOf(sav)))
            return (null, EntityConverterResult.NoTransferRoute);

        var converted = EntityConverter.ConvertToType(pk, sav.PKMType, out var result);
        if (converted is null)
            return (null, result);

        sav.AdaptToSaveFile(converted);
        return (converted, result);
    }

    public static byte FormatOf(SaveFile sav) => sav.PKMType.Name switch
    {
        "PK6" => 6,
        "PK7" => 7,
        "PK8" => 8,
        "PB8" => 8,
        "PA8" => 8,
        "PK9" => 9,
        _ => sav.Generation,
    };

    public static string Sha256Hex(byte[] data) => Convert.ToHexString(SHA256.HashData(data)).ToLowerInvariant();

    public string SpeciesName(ushort species)
    {
        try
        {
            var strings = GameInfo.GetStrings("en");
            if (species < strings.Species.Count)
                return strings.Species[species];
        }
        catch
        {
            // ignore missing string tables
        }

        return species == 0 ? "(empty)" : $"#{species}";
    }
}
