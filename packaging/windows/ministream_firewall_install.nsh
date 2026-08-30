  MessageBox MB_YESNO|MB_ICONQUESTION "Allow MiniStream to receive streaming traffic on Private Networks?" IDNO MiniStreamFirewallDone
  ExecWait 'netsh advfirewall firewall add rule name="MiniStream (Private UDP)" dir=in action=allow program="$INSTDIR\bin\ministream.exe" protocol=UDP profile=private enable=yes' $0
  MiniStreamFirewallDone:
