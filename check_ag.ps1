Param
(
    # The name of the server instance that hosts the availability group
    [Parameter(Mandatory=$true)]
    [string] $ServerName,

    # Name of the availability group to monitor
    [Parameter(Mandatory=$true)]
    [string] $GroupName
)
Import-Module Sqlps -ArgumentList $ServerName, $GroupName -DisableNameChecking

# Connect to the server instance and set default init fields for 
# efficient loading of collections. We use windows authentication here,
# but this can be changed to use SQL Authentication if required.
$serverObject = New-Object Microsoft.SqlServer.Management.SMO.Server($ServerName)
$serverObject.SetDefaultInitFields([Microsoft.SqlServer.Management.Smo.AvailabilityGroup], $true)
$serverObject.SetDefaultInitFields([Microsoft.SqlServer.Management.Smo.AvailabilityReplica], $true)
$serverObject.SetDefaultInitFields([Microsoft.SqlServer.Management.Smo.DatabaseReplicaState], $true)

# Attempt to access the availability group object on the server
$groupObject = $serverObject.AvailabilityGroups[$GroupName]

if($groupObject -eq $null)
{
    # Can't find the availability group on the server.
	Write-Host "The availability group '$GroupName' does not exist on server '$ServerName'."
	exit 2
}
elseif($groupObject.PrimaryReplicaServerName -eq $null)
{
    # Can't determine the primary server instance. This can be serious (may mean the AG is offline), so throw an error.
	Write-Host "Cannot determine the primary replica of availability group '$GroupName' from server instance '$ServerName'. Please investigate!"
	exit 2
}
elseif($groupObject.PrimaryReplicaServerName -ne $ServerName)
{
    # We're trying to run the script on a secondary replica, which we shouldn't do.
	Write-Host "The server instance '$ServerName' is not the primary replica for the availability group '$GroupName'. Skipping evaluation."
	exit 2
}
else
{
    # Run the health cmdlets
    $groupResult = Test-SqlAvailabilityGroup $groupObject -NoRefresh
    $replicaResults = @($groupObject.AvailabilityReplicas | Test-SqlAvailabilityReplica -NoRefresh)
    $databaseResults = @($groupObject.DatabaseReplicaStates | Test-SqlDatabaseReplicaState -NoRefresh)
	
	# Run the sync cmdlet and determine if databases are in sync
	$availabilityDatabase = @($groupObject.AvailabilityDatabases)
	$availabilityDatabaseNotSync = @($availabilityDatabase | Where-Object {$_.SynchronizationState -ne "Synchronized"})
    
    # Determine if any objects are in the critical state
    $groupIsCritical = @($groupResult | Where-Object {$_.HealthState -eq "Error"})
    $criticalReplicas = @($replicaResults | Where-Object {$_.HealthState -eq "Error"})
    $criticalDatabases = @($databaseResults | Where-Object {$_.HealthState -eq "Error"})
	
	# Determine if any objects are in the warning state
	$groupIsWarning = @($groupResult | Where-Object {$_.HealthState -eq "Warning"})
    $warningReplicas = @($replicaResults | Where-Object {$_.HealthState -eq "Warning"})
    $warningDatabases = @($databaseResults | Where-Object {$_.HealthState -eq "Warning"})

    # If any objects are critical throw an error
    if($groupIsCritical.Count -gt 0 -or $criticalReplicas.Count -gt 0 -or $criticalDatabases.Count -gt 0)
    {
		Write-Host -NoNewline "Critical - "
		if($groupIsCritical.Count -gt 0)
		{
			Write-Host -NoNewline "Availability Group - " $groupIsCritical.Name
		}
		if($criticalReplicas.Count -gt 0)
		{
			Write-Host -NoNewline "Availability Replica - " $criticalReplicas.Name
		}
		if($criticalDatabases.Count -gt 0)
		{
			Write-Host -NoNewline "Database Replica State - " $criticalDatabases.Name
		}
		exit 2
    }
	# If any objects are in a warning state throw a warning
	elseif($groupIsWarning.Count -gt 0 -or $warningReplicas.Count -gt 0 -or $warningDatabases.Count -gt 0)
	{
		Write-Host -NoNewline "Warning - "
		if($groupIsWarning.Count -gt 0)
		{
			Write-Host -NoNewline "Availability Group - " $groupIsWarning.Name
		}
		if($warningReplicas.Count -gt 0)
		{
			Write-Host -NoNewline "Availability Replica - " $warningReplicas.Name
		}
		if($warningDatabases.Count -gt 0)
		{
			Write-Host -NoNewline "Database Replica State - " $warningDatabases.Name
		}
		exit 1
	}
	else
	{
		if($availabilityDatabaseNotSync.Count -gt 0)
		{
			Write-Host -NoNewline "Failed to synchronize - " $availabilityDatabaseNotSync.Name
			exit 2
		}
		else
		{
			Write-Host "All objects in the availability group '$GroupName' are healthy."
			exit 0
		}
	}
}