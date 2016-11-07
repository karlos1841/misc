Param(
  [string]$ARG1,
  [string]$ARG2,
  [string]$ARG3,
  [int]$WARN,
  [int]$ERR
)
$pass=convertto-securestring -String $ARG3 -AsPlainText -Force
$cred=new-object -typename System.Management.Automation.PSCredential($ARG2, $pass)

$TimeStart=Get-Date
$TimeEnd=$timeStart.AddSeconds(20)
while($true)
{
	$TimeNow=Get-Date
	if($TimeNow -ge $TimeEnd)
	{
		Write-Host "Error: It's been running too long now. Terminating..."
		exit 2
	}
	else
	{
		New-PSDrive -Name 'O' -PSProvider FileSystem -Root $ARG1 -Credential $cred -Persist 2>&1 | Out-Null
		if($? -eq $true){break}
		Start-Sleep -s 1
	}
}

$name=(Get-PSDrive | Where-Object {$_.name -eq 'O'}).name
$freeGB=(Get-PSDrive | Where-Object {$_.name -eq 'O'}).free / (1024*1024*1024)
$usedGB=(Get-PSDrive | Where-Object {$_.name -eq 'O'}).used / (1024*1024*1024)
$fullGB=$freeGB+$usedGB

$roundedfreeGB=[math]::round($freeGB, 2)
$roundedusedGB=[math]::round($usedGB, 2)
$roundedfullGB=[math]::round($fullGB, 2)

Remove-PSDrive -Name 'O' 2>&1 | Out-Null
if($? -ne $true){Write-Host "Error: Unmapping the device failed"; exit 2}


if($usedGB -ge ($ERR * $fullGB / 100))
{
	Write-Host -NoNewline "Critical:" "Name:" $name "/" "ProviderName:" $ARG1 "/" "Used space:" $roundedusedGB "GB" "/" "Free space:" $roundedfreeGB "GB" "/" "Capacity:" $roundedfullGB "GB"; exit 2
}
elseif(($usedGB -lt ($ERR * $fullGB / 100)) -and ($usedGB -ge ($WARN * $fullGB / 100)))
{
	Write-Host -NoNewline "Warning:" "Name:" $name "/" "ProviderName:" $ARG1 "/" "Used space:" $roundedusedGB "GB" "/" "Free space:" $roundedfreeGB "GB" "/" "Capacity:" $roundedfullGB "GB"; exit 1
}
else
{
	Write-Host -NoNewline "OK:" "Name:" $name "/" "ProviderName:" $ARG1 "/" "Used space:" $roundedusedGB "GB" "/" "Free space:" $roundedfreeGB "GB" "/" "Capacity:" $roundedfullGB "GB"; exit 0
}