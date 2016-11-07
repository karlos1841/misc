Param(
	[string]$LOGNAME,
	[string]$ENTRYTYPE,
	[int]$ARG1,
	[int[]]$ARG2
)
$cmd=(Get-EventLog -LogName $LOGNAME -EntryType $ENTRYTYPE -After (Get-Date).AddHours(-$ARG1) | Where-Object {$ARG2.Contains($_.EventID)})
$lines=($cmd | Measure-Object -Line).lines
$eventid=($cmd.EventID | select -Unique)
if($lines -gt 0){if($lines -eq 1){Write-Host -NoNewline "Critical: Found" $lines "entry"} else{Write-Host -NoNewline "Critical: Found" $lines "entries"}; foreach($i in $eventid){Write-Host -NoNewline ","$i}; Write-Host -NoNewline "|entries=$lines"; exit 2}
else{Write-Host -NoNewline "OK: No entries found|entries=$lines"; exit 0}