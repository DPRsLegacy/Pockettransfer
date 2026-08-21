using PKHeX.Core;

namespace Pockettransfer.Server.Services;

/// <summary>
/// Bank / Transporter / Home-like transfer gates: PKHeX conversion plus destination-game format.
/// Backward transfers are rejected when PKHeX has no route.
/// </summary>
public sealed class ConversionRules(PkHexService pkhex)
{
    public bool CanTransferToSave(PKM pk, SaveFile dest, out string reason)
    {
        if (pk.Species == 0)
        {
            reason = "Empty slot.";
            return false;
        }

        if (pk.GetType() == dest.PKMType)
        {
            reason = "Already destination format.";
            return true;
        }

        var destFormat = PkHexService.FormatOf(dest);
        if (!EntityConverter.IsConvertibleToFormat(pk, destFormat))
        {
            reason = $"No transfer route from format {pk.Format} ({pk.GetType().Name}) to {dest.PKMType.Name}.";
            return false;
        }

        var (converted, result) = pkhex.ConvertToSave(pk.Clone(), dest);
        if (converted is null)
        {
            reason = $"Conversion failed: {result}.";
            return false;
        }

        if (converted.Species > dest.MaxSpeciesID)
        {
            reason = $"{converted.Species} does not exist in the destination game.";
            return false;
        }

        reason = result is EntityConverterResult.None or EntityConverterResult.Success
            ? "Convertible."
            : result.ToString();
        return true;
    }
}
