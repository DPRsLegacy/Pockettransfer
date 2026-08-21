using System.Security.Cryptography;
using PKHeX.Core;

namespace Pockettransfer.Server.Services;

public sealed class LegalitySummary
{
    public bool Valid { get; init; }
    public required string Report { get; init; }
    public IReadOnlyList<string> Issues { get; init; } = [];
}

public sealed record PokeSummary(
    int Gender,
    int Tid,
    string Nature,
    string Type1,
    string Type2,
    string MetDate,
    int IvHp,
    int IvAtk,
    int IvDef,
    int IvSpa,
    int IvSpd,
    int IvSpe)
{
    public static PokeSummary Empty { get; } = new(2, 0, "", "", "", "", 0, 0, 0, 0, 0, 0);
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
        "PK1" => 1,
        "PK2" => 2,
        "PK3" => 3,
        "PK4" => 4,
        "PK5" => 5,
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

    public PokeSummary Summarize(PKM pk)
    {
        if (pk.Species == 0)
            return PokeSummary.Empty;

        var strings = GameInfo.GetStrings("en");
        var type1 = "";
        var type2 = "";
        try
        {
            var pi = pk.PersonalInfo;
            if (pi.Type1 < strings.Types.Count)
                type1 = strings.Types[pi.Type1];
            if (pi.Type2 != pi.Type1 && pi.Type2 < strings.Types.Count)
                type2 = strings.Types[pi.Type2];
        }
        catch
        {
            // personal table missing for this format
        }

        var nature = "";
        try
        {
            var n = (int)pk.Nature;
            if ((uint)n < (uint)strings.Natures.Count)
                nature = strings.Natures[n];
        }
        catch
        {
            // ignore
        }

        var met = "";
        try
        {
            if (pk.MetDate is { } d && d.Month > 0 && d.Day > 0)
                met = $"{d.Month}/{d.Day}/{d.Year % 100:D2}";
        }
        catch
        {
            // ignore
        }

        var tid = 0;
        try
        {
            tid = (int)pk.DisplayTID;
        }
        catch
        {
            tid = pk.TID16;
        }

        return new PokeSummary(
            pk.Gender,
            tid,
            nature,
            type1,
            type2,
            met,
            pk.IV_HP,
            pk.IV_ATK,
            pk.IV_DEF,
            pk.IV_SPA,
            pk.IV_SPD,
            pk.IV_SPE);
    }

    public PokeSummary SummarizeStored(byte[] data, int entityContext)
    {
        try
        {
            var pk = LoadPkm(data, (EntityContext)entityContext) ?? LoadPkmBySize(data);
            return pk is null ? PokeSummary.Empty : Summarize(pk);
        }
        catch
        {
            return PokeSummary.Empty;
        }
    }
}
