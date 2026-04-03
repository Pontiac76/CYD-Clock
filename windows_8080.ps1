$Port = 8080
$RuleName = "WSL Port $Port"

$WslIp = (wsl hostname -I).Trim().Split()[0]

if (-not $WslIp) {
    Write-Error "Could not determine WSL IP."
    exit 1
}

Write-Host "WSL IP: $WslIp"

netsh interface portproxy delete v4tov4 listenaddress=0.0.0.0 listenport=$Port | Out-Null
netsh interface portproxy add v4tov4 listenaddress=0.0.0.0 listenport=$Port connectaddress=$WslIp connectport=$Port

$ExistingRule = Get-NetFirewallRule -DisplayName $RuleName -ErrorAction SilentlyContinue
if (-not $ExistingRule) {
    New-NetFirewallRule -DisplayName $RuleName -Direction Inbound -Protocol TCP -LocalPort $Port -Action Allow | Out-Null
    Write-Host "Created firewall rule: $RuleName"
} else {
    Write-Host "Firewall rule already exists: $RuleName"
}

Write-Host ""
Write-Host "Current portproxy:"
netsh interface portproxy show v4tov4
