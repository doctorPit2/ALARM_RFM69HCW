'##############################################################
'TestEmpfaengerRNMikroFunk.bas
'Programm läuft auch auf Pollin Funk _ Eval Board
'Controlle am Seriell Port
'
'$regfile = "m8adef.dat"
$regfile = "m328pdef.dat" Rn Minifunkboard
$framesize = 32
$swstack = 32
$hwstack = 64
'$crystal = 12000000 Rn Minifunkboard
$crystal = 8000000                                'Quarzfrequenz
$baud = 9600                                      'Baudrate (Übertragungsgeschwindigkeit)
Baud = 9600

Declare Sub Impuls()
Declare Sub Rfm12_init
Declare Sub Rfm12_empfange(byval Anzahl As Word)
Declare Sub Playhund1()
Declare Sub Playhund2()

Declare Function Spitransfer(byval Dout As Word) As Word



Ss Alias Portb.2
Mosi Alias Portb.3
Miso Alias Pinb.4
Sck Alias Portb.5

Config Ss = Output
Config Mosi = Output
Config Sck = Output

'config DCF77 = PIND.4, Timer = 1, Debug = 1, Check = 1, Inverted = 0, UPdate = 2, Gosub = Sectic
Config Timer1 = Timer , Prescale = 8

Config Portd.4 = Output                           'LED_Funk
Config Portd.3 = Output                           'LED_Stat
Config Portd.5 = Output                           'Mute_Sound
Config Portd.6 = Output                           'Sound 1 play

Config Portc.2 = Output                           'Enable_Sound
Config Portc.3 = Output                           'Sound 2 play

Config Portb.0 = Input                            'Taster Alarm EIN/AUS
Config Portc.0 = Output                           'Beep



Portb.0 = 1
Portb.1 = 0
Portc.2 = 1
Portc.3 = 1
Portd.5 = 1
Portd.6 = 1

               'Blink count
Dim Aktiv As Bit

Dim Auswert As String * 4                         'select case Auswert in Hauptschleife
Dim Eingangsdaten(5) As Byte
Dim Eingangsstring As String * 4 At Eingangsdaten Overlay
Dim Datenbyteanzahl As Word

Dim Cntsabo As Byte                               'Steht sabo 2sec lang an? 100ms Zähler
Dim Cntalvo As Byte                               'steht AlVo 2sec lang an? 100ms Zähler
Dim Cntalhi As Byte                               'Steht ALHI 2sec lang an? 100ms Zähler
Dim Cntalarmstart As Byte                         'Alarm erst nach 3 sec einschalten

Dim Flagsabo As Bit                               'Sabo Stand 2sec lang an
Dim Flagalvo As Bit                               'ALVO Stand 2sec lang an
Dim Flagalhi As Bit                               'AlHI Stand 2sec lang an
Dim Flagalarm As Bit                              '3 sec sind rum
Dim Hundbellt As Bit                              '30 sec MP3 Sperre


Dim Sabo As Bit                                   'von Auswert Funk
Dim Alvo As Bit                                   'von Auswert Funk
Dim Alhi As Bit                                   'von Auswert Funk
Dim Dunkel As Bit                                 'von Auswert Funk

Dim N As Byte
Dim Takt As Byte                                  '100ms Timertakt
Dim Cnttaktsnd As Word                            '100ms Takt Ablaufsteuerung Alarmierung
Taster Alias Pinb.0                               'Alarm Enable
Beep Alias Portc.0                                'Beep bei Sabotage
Led_funk Alias Portd.4                            'blinkt bei jedem empfangenen Datenpaket
Led_stat Alias Portd.3                            'Leuchtet bei Alarm enabled
Mute Alias Portd.5                                'Mutet das Soundmodul
Snd1 Alias Portd.6                                'Spielt Sound1 ab
Ensnd Alias Portc.2                               'schaltet das Soundmodul ein
Snd2 Alias Portc.3                                'Spielt Sound2 ab

Mute = 1

Snd1 = 1

Snd2 = 1

Waitms 100
Ss = 1
Sck = 0
Rfm12_init

Print "Bin da..."


Enable Timer1
Enable Interrupts


On Timer1 Waechter
'Hauptprogramm
'~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Do

    Datenbyteanzahl = 4

    Call Rfm12_empfange(datenbyteanzahl)          'gehe auf Empfang und warte
                                          'auf die 4 erwarteten Datenbytes

    Auswert = Left(eingangsstring , Datenbyteanzahl)

    Print Auswert

    Select Case Auswert

      Case "nixx" : Toggle Mute : Takt = 0 : Sabo = 0 : Alvo = 0 : Alhi = 0
      Case "TAVO" : Sabo = 1
      Case "TAHI" : Sabo = 1
      Case "ALVO" : Alvo = 1
      Case "ALHI" : Alhi = 1
      Case "dark" : Dunkel = 1 : Takt = 0

    End Select

        Auswert = ""

     If Cntalvo >= 20 Then                        'länger als 2 sec

         Flagalvo = 1

         Cntalvo = 0

     End If


     If Cntalhi >= 20 Then                        'länger als 2 sec

         Flagalhi = 1

         Cntalhi = 0

     End If

     If Cntalarmstart = 30 Then

         If Flagalhi = 1 Then

            Cntalarmstart = 0

            Flagalhi = 0

            Call Playhund2

            Hundbellt = 1

            Elseif Flagalvo = 1 Then

            Cntalarmstart = 0

            Flagalvo = 0

            Call Playhund1

            Hundbellt = 1

         End If

     End If

If Cnttaktsnd = 400 Then Hundbellt = 0


    Loop
 '~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 '###############################
Waechter:

If Hundbellt = 1 Then Goto Sprung

Incr Takt

If Alvo = 1 Then

   Incr Cntalvo

Else

   Cntalvo = 0

End If

If Alhi = 1 Then

   Incr Cntalhi

Else

   Cntalhi = 0

End If

If Flagalhi = 1 Or Flagalvo = 1 Then

   Incr Cntalarmstart                             '3 sec

Else

  Cntalarmstart = 0

End If

Sprung:

If Hundbellt = 1 Then
Incr Cnttaktsnd                                   'word bis 65535
End If

If Takt >= 20 Then

   Toggle Snd1

  'Print "Alarm  " ; Auswert

  Beep = 1

  Call Impuls

   Takt = 0

   Timer1 = 15535                                 'für 100ms

   End If



Return

'###############################


 ' ************ Hilfsfunktionen zur Kommunikation mit Funkmodul **************
'  (nähere Infos im Datenblatt des Funkmoduls)


' Initialisiere Funkmodul

Sub Rfm12_init

   Local Wert As Word
   Local X As Word
   Local D As Word

  X = 0
  Restore Datainit3                               'Initialisierungsfolge
  Do
      Read Wert
      D = Spitransfer(wert)
      Incr X
  Loop Until Wert = 0

  Waitms 200

  End Sub


Sub Rfm12_empfange(byval Anzahl As Word)

Local D As Word

  D = Spitransfer(&H82c8)
  D = Spitransfer(&Hca83)
  For N = 1 To Anzahl
    Ss = 0
    Do
      Waitus 100
    Loop Until Miso = 1
    D = Spitransfer(&Hb000)
    Eingangsdaten(n) = D
  Next N
  D = Spitransfer(&H8208)

  End Sub

Sub Impuls()



End Sub

Function Spitransfer(byval Dout As Word) As Word

Local Nspi As Integer
Local Dspi As Integer
Local Dmiso As Word
  Ss = 0
  Dmiso = 0
  For Nspi = 1 To 16
    Dspi = Dout And &H8000
    If Dspi = 0 Then
       Mosi = 0
    Else
       Mosi = 1
    End If
    Dout = Dout * 2
    Dmiso = Dmiso * 2
    Dmiso = Dmiso + Miso
    Sck = 1
    Waitus 5
    Sck = 0
  Next Nspi
  Ss = 1
  Spitransfer = Dmiso

  End Function

Rfm12_warte:
     Ss = 0
     Do
     Loop Until Miso = 1
Return



Sub Playhund1()

Reset Ensnd

Set Snd1

Waitms 900

Reset Snd1

Set Ensnd

End Sub


Sub Playhund2()

Reset Ensnd

Set Snd2

Waitms 900

Reset Snd2

Set Ensnd

End Sub
End

'Funkmodul Initialisierungsdaten  9600 Baud

Datainit3:
Data &H80D7%                                      'OK Enable: 433 Mhz;XTAL cap=12pf; TX-Register; RX-Fifo
Data &H82D9%                                      'OK Enable: Receiver; Crystal Osc; Base Band Block; Synthesizer
Data &HA67C%                                      'OK &Ha67c=frequenz 434,15 Mhz  oder z.B. &HA538 für 433,34 Mhz
Data &HC623%                                      'OK &Hc647=Datenrate '4.8kbps;  C623=9600kbps;  C611 =19200
Data &H90C0%                                      'OK LNA Gain=MAX; Pin=nInt; RX Bandwidth=67 khz; VDI=Fast; DRSSI=-103 dB
Data &HC2AC%                                      'OK Fiter=Digital; Recover Mode=Auto; Quality Threshold=4; Recovery Speed=Slow
Data &HCA80%                                      'OK FIFO INT Level=8; Sync on=2;Fifo Fill Start=Sync; Reset Sensitivity=High;                                                            'Disable:FIFO Fill Enable
Data &HC483%                                      'OK Enable: AFC Mode; AFC; Frequency Offset Register                                                            'Disable: High Accuracy; Strobe
Data &H9820%                                      'OK Frequenz Shift=POS; Power Out=0 dB; Deviation=45 khz
Data &HE000%                                      'OK WakeUp-Timer=0ms
Data &HC800%                                      'OK Duty Cycle = Infinity % OFF
Data &HC000%                                      'OK Low batterie=2,2V; Clock Pin=1 Mhz
Data &HCED4%                                      'OK Synchron Pattern
Data &HCC57%                                      'PLL Settings
Data &H0000%                                      'Status lesen irqs zurückstellen
Data 0%                                           'ende initialisierung