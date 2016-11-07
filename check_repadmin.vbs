if wscript.arguments.count <> 2 then
	wscript.echo "Unknown: number of arguments"
	wscript.Quit(3)
end if

Dim destinationDSA, sourceDSA
Dim namingContextString, showreplString, fullOutput

Dim WshShell, oExec
Dim rowArray, foundArray, fieldArray, showreplArray, statusArray
Dim statusSum
Dim output, i

Set WshShell = CreateObject("WScript.Shell")
statusSum = 0
i = 0

destinationDSA = wscript.arguments.item(0)
sourceDSA = wscript.arguments.item(1)

Set oExec    = WshShell.Exec("c:\Windows\System32\repadmin.exe /showrepl /csv")
output = oExec.stdout.readAll

if len(output) = 0 then
	wscript.echo "Unknown: output from external command"
	wscript.Quit(3)
end if

rowArray = split(output, vbCrLf)

Redim foundArray(i)
for each row in rowArray
	if (instr(row, destinationDSA) <> 0) and (instr(row, sourceDSA) <> 0) then
		Redim preserve foundArray(i)
		foundArray(i) = row
		i = i + 1
	end if
next
if i = 0 then
	wscript.echo "Unknown: destination DSA / source DSA"
	wscript.Quit(3)
end if


for each foundRow in foundArray
	fieldArray = split(foundRow, """")
	showreplArray = split(fieldArray(0), ",")
	statusArray = split(fieldArray(2), ",")
	
	if instr(namingContextString, fieldArray(1)) = 0 then
		namingContextString = namingContextString + fieldArray(1) + " "
		fullOutput = fullOutput + namingContextString + "/ Number of Failures: " + statusArray(4) + " / Last Failure Time: " + statusArray(5) + " / Last Failure Status: " + statusArray(7) + " "
		showreplString = showreplString + showreplArray(0) + " "
		statusSum = statusSum + statusArray(4)
	end if
next

if instr(showreplString, "showrepl_ERROR") then
	wscript.echo "Error: Host nieosiagalny sieciowo" + " / Destination DSA: " + destinationDSA + " / Source DSA: " + sourceDSA + " / Naming Context: " + namingContextString
	wscript.Quit(2)
end if

if statusSum <> 0 then
	wscript.echo "Error: Blad replikacji" + " / Destination DSA: " + destinationDSA + " / Source DSA: " + sourceDSA + " / Naming Context: " + fullOutput
	wscript.Quit(2)
end if

wscript.echo "OK: " + "Destination DSA: " + destinationDSA + " / Source DSA: " + sourceDSA + " / Naming Context: " + namingContextString
wscript.Quit(0)