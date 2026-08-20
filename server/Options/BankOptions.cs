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
    /// <summary>
    /// Comma-separated usernames that are always promoted to admin on startup.
    /// </summary>
    public string AdminUsernames { get; set; } = "";
    public int LoginFailureLimit { get; set; } = 5;
    public int LoginIpAttemptLimit { get; set; } = 20;
    public int LoginWindowMinutes { get; set; } = 15;
}
