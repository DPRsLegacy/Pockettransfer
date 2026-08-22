namespace Pockettransfer.Server.Data;

public sealed class DataPaths(string root)
{
    public string Root { get; } = root;
    public string Backups { get; } = Path.Combine(root, "backups");
}
