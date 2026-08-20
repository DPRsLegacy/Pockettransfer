namespace Pockettransfer.Server.Options;

public sealed class BankOptions
{
    public const string SectionName = "Bank";
    public bool AllowIllegalDeposit { get; set; }
    public bool AllowIllegalWithdraw { get; set; }
    public int BoxCount { get; set; } = 32;
    public int SlotsPerBox { get; set; } = 30;
    public int SessionMinutes { get; set; } = 20;
    public int MaxSaveBytes { get; set; } = 8_388_608;
    public int PairingMinutes { get; set; } = 10;
}
