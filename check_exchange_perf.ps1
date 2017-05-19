Param(
	#[string]$counter="\MSExchangeTransport Queues(_total)\Items Completed Delivery TOTAL",
	[string]$counter="",
	[int]$sample=10,
	[double]$step=1,
	[int[]]$WARN=@(0),
	[int[]]$ERR=@(0),
	[string]$type="avg"
)
if(!$counter -or $counter -eq "--help" -or ($type -ne "avg" -and $type -ne "min" -and $type -ne "max"))
{
	Write-Host "check_exchange_perf <counter> [sample] [step] [WARN] [ERR] [type]
	counter - name of the counter
	sample - number of samples, default: 10
	step - seconds, default: one sample per 1 second
	WARN - warning threshold, default: 0, for multiple counters add the same amount of thresholds comma separated
	ERR - critical threshold, default: 0, for multiple counters add the same amount of thresholds comma separated
	type - default is to calculate average, available options: avg, min, max"
	exit 0
}
if((Get-PSSnapin -Name Microsoft.Exchange.Management.PowerShell.E2010 -ErrorAction:SilentlyContinue) -eq $null)
{
	Add-PSSnapin Microsoft.Exchange.Management.PowerShell.E2010 2> $null
}

try
{
	$c = {(Get-Counter $counter -ErrorAction Stop).countersamples}
	if($sample -eq -99){((& $c)).Path;exit 0} #debug
	(& $c) | Out-Null
}
catch
{
	Write-Host "Error: Counter was not found"
	exit 2
}

$perfdata = ""
$value = ""
$statusCode=New-Object System.Collections.ArrayList
$customCounter = (& $c)
$customCounter | Add-Member -Name 'warning' -Type NoteProperty -Value $WARN[0]
$customCounter | Add-Member -Name 'critical' -Type NoteProperty -Value $ERR[0]
$inc = 0
while($inc -lt $WARN.count)
{
	$customCounter[$inc].warning = $WARN[$inc]
	$inc = $inc + 1
}
$inc = 0
while($inc -lt $ERR.count)
{
	$customCounter[$inc].critical = $ERR[$inc]
	$inc = $inc + 1
}

foreach($a in $customCounter)
{
	$d = {(Get-Counter $a.path).countersamples.cookedvalue}
	if(!(& $d)){continue}
	$counterName = $a.instancename -replace ' ', '_'
	$warning = $a.warning
	$critical = $a.critical

	$array=New-Object System.Collections.ArrayList
	$sleep=[math]::Round($step,3)*1000

	$x=$sample
	while($x -gt 0)
	{
		$array.Add((& $d)) | Out-Null
		$x-=1
		Start-Sleep -m $sleep
	}

	if($type -eq "max")
	{
		$max=0
		$max=($array | measure -Maximum).Maximum
		$max=[math]::Round($max,2)
		if($max -gt $critical)
		{
			$status=2
		}
		elseif(($max -le $critical) -and ($max -gt $warning))
		{
			$status=1
		}
		else
		{
			$status=0
		}

		$value = $value + $counterName + ": " + $max + " "
		$perfdata = $perfdata + $counterName + "=" + $max + ";" + $warning + ";" + $critical + " "
		$statusCode.Add($status) | Out-Null
	}
	elseif($type -eq "min")
	{
		$min=0
		$min=($array | measure -Minimum).Minimum
		$min=[math]::Round($min,2)
		if($min -gt $critical)
		{
			$status=2
		}
		elseif(($min -le $critical) -and ($min -gt $warning))
		{
			$status=1
		}
		else
		{
			$status=0
		}

		$value = $value + $counterName + ": " + $min + " "
		$perfdata = $perfdata + $counterName + "=" + $min + ";" + $warning + ";" + $critical + " "
		$statusCode.Add($status) | Out-Null
	}
	else #default average
	{
		$sum=0
		foreach($i in $array)
		{
			$sum+=$i
		}

		$average=$sum/$sample
		$average=[math]::Round($average,2)
		if($average -gt $critical)
		{
			$status=2
		}
		elseif(($average -le $critical) -and ($average -gt $warning))
		{
			$status=1
		}
		else
		{
			$status=0
		}

		$value = $value + $counterName + ": " + $average + " "
		$perfdata = $perfdata + $counterName + "=" + $average + ";" + $warning + ";" + $critical + " "
		$statusCode.Add($status) | Out-Null
	}
}

if($statusCode.Contains(2))
{
	Write-Host "Critical:"$type": "$value"\ Samples: "$sample" \ One sample per "$step"s | "$perfdata
	exit 2
}
elseif($statusCode.Contains(1))
{
	Write-Host "Warning:"$type": "$value"\ Samples: "$sample" \ One sample per "$step"s | "$perfdata
	exit 1
}
else
{
	Write-Host "OK:"$type": "$value"\ Samples: "$sample" \ One sample per "$step"s | "$perfdata
	exit 0
}