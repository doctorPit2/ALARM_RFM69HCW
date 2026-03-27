'##############################################################
'BlinkAll.bas
'
'Ein  Testprogramm für die Universalplatine RN-MikroFunk
'
'Das Programm testet die Alarmeingänge der Aussenseite un lässt
'die rote LED bei Alarm, und die blaue LED bei Sabotage blinken
'Der LDR lässt bei dunkel beide LED's abwechselnd blinken

'######################################################################

'$programmer = 12   'MCS USB  (Zeile weglassen wenn anderer Programmer)

$PROG &HFF,&HFF,&HD9,&HFF 'Diese Anweisung stellt Fusebits ein

$regfile = "m8def.dat"
'$regfile = "m328pdef.dat"
$framesize = 32
$swstack = 32
$hwstack = 64

$crystal = 8000000     'Resonatorfrequenz

Declare Sub Rfm12_init
Declare Sub Rfm12_senden(byval Anzahl As Integer)
Declare Function Spitransfer(byval Dout As Word) As Word


Ss Alias Portb.2
Mosi Alias Portb.3
Miso Alias Pinb.4
Sck Alias Portb.5

Config Ss = Output
Config Mosi = Output
Config Sck = Output



Config PinC.5 = Output   'LEDAlarm
Config PinC.4 = Output   'LEDSabo
Config PinC.3 = Input    'LDR Eingang


Config PinD.5 = Input    'ALHI
Config PIND.6 = Input    'TAHI
Config PIND.1 = Input    'ALVO
Config PIND.0 = Input    'TAVO

Config PInB.1 = Input 'PCint1
Config PInB.0 = Input 'PCint0


'config DCF77 = PIND.4, Timer = 1, Debug = 1, Check = 1, Inverted = 0, UPdate = 2, Gosub = Sectic


Config Timer1 = Timer , Prescale = 8

Enable Timer1
on timer1 Takt_isr


Portb.1 = 1
Portb.0 = 1

Dim cnt as Byte
Dim cntevent as byte
Dim Flag_event0 as  Bit
Dim Event as  Byte

Dim Sabo as  Bit  'Talem angesprochen
Dim Alarm as  Bit 'Alarm angesprochen
Dim Takt as Byte

Dim Ausgabedaten(5) As Byte
dim ausgabestring as string*4  At Ausgabedaten Overlay

Ledalarm Alias portc.5
Ledsabo Alias Portc.4
LDR  Alias portc.3

Tavo Alias Portd.0
Alvo Alias Portd.1
Alhi Alias PortD.5
Tahi Alias  PortD.6

PORTD.0 = 1
Portd.1 = 1
PORTD.5 = 1
PORTD.6 = 1

PortC.3 = 1 'LDR

'Pcmsk0 = &B00000010  'Pin Change Mask register              00000111
'PCICR = &B00000011   'Pin Change Interupt Control Register  00000111
'PCIFR = &B00000011   'Pin Change interupt Flag Register     00000111


Enable INTERRUPTS



Reset Ledalarm


'on Pcint0 Event_isr  'Sprung zur Interrupt - Routine

Ss = 1
Sck = 0
Rfm12_init

Do

If Flag_event0 = 1 Then

  call Rfm12_senden(4)

   Flag_event0 = 0
   Sabo = 0
   Alarm = 0

elseif Flag_event0 = 0 Then

   ausgabestring = "nixx"
   call Rfm12_senden(4)
   wait 1
End IF

If PINC.3 = 1 Then  'LDR


Flag_event0 = 1
ausgabestring = "dark"

   'set LEDalarm
   'waitms 200
   'set ledsabo
   'waitms 400
   'Reset Ledalarm
   'Reset Ledsabo

   End if
Loop
End

Sub Rfm12_init
Local Wert As Word
Local X As Word
Local d As Word

  X = 0
  Restore Datainit3                  'Initialisierungsfolge
  Do
      Read Wert
      D = Spitransfer(wert)
      Incr X
  Loop Until Wert = 0
  Waitms 200
End Sub


Sub Rfm12_senden(byval Anzahl As Integer)
local n as byte
Local d As Word

  D = Spitransfer(&H8238)                                   'Enable Transmitter; enable Synthesizer ;enable Crystal Osc
  Gosub Rfm12_warte
  D = Spitransfer(&Hb8aa)
  Gosub Rfm12_warte
  D = Spitransfer(&Hb8aa)
  Gosub Rfm12_warte
  D = Spitransfer(&Hb8aa)
  Gosub Rfm12_warte
  D = Spitransfer(&Hb82d)
  Gosub Rfm12_warte
  D = Spitransfer(&Hb8d4)
  For N = 1 To Anzahl
    Gosub Rfm12_warte
    D = &HB800 + Ausgabedaten(n)
    D = Spitransfer(d)
  Next N
  Gosub Rfm12_warte
  D = Spitransfer(&Hb8aa)
  Gosub Rfm12_warte
  D = Spitransfer(&Hb8aa)
  Gosub Rfm12_warte
  D = Spitransfer(&H8208)                                   'disable transmitter und  receiver
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

End ' programmende

Event_isr:    '~~~~~~~~~~~~~Interrupt - Routine~~~~~~~~~~~~

   If PIND.0 = 0 Then         'TAVO
      cntevent = 2
      Sabo = 1
      ausgabestring = "TAVO"

   ElseiF PIND.1 = 0  Then    'ALVO
      cntevent = 3
      Alarm =1
      ausgabestring = "ALVO"
   Elseif PIND.5 = 0 Then     'ALHI
      cntevent = 2
      Alarm =1
      ausgabestring = "ALHI"
   Elseif PIND.6 = 0 Then     'TAHI
      cntevent = 3
      Sabo = 1
      ausgabestring = "TAHI"

   End if

Flag_event0 = 1

return

Takt_ISR:

Incr Takt

If takt >= 8 Then

   Toggle LEDSabo

   Takt = 0

End if

Return


'Funkmodul Initialisierungsdaten mit 9600 Baud

Datainit3:
Data &H80D7%                                                'OK Enable: 433 Mhz;XTAL cap=12pf; TX-Register; RX-Fifo
Data &H82D9%                                                'OK Enable: Receiver; Crystal Osc; Base Band Block; Synthesizer
Data &HA67C%                                                'OK &Ha67c=frequenz 434,15 Mhz  oder z.B. &HA538 für 433,34 Mhz
Data &HC623%                                                'OK &Hc647=Datenrate '4.8kbps;  C623=9600kbps;  C611 =19200
Data &H90C0%                                                'OK LNA Gain=MAX; Pin=nInt; RX Bandwidth=67 khz; VDI=Fast; DRSSI=-103 dB
Data &HC2AC%                                                'OK Fiter=Digital; Recover Mode=Auto; Quality Threshold=4; Recovery Speed=Slow
Data &HCA80%                                                'OK FIFO INT Level=8; Sync on=2;Fifo Fill Start=Sync; Reset Sensitivity=High;                                                          'Disable:FIFO Fill Enabled
Data &HC483%                                                'OK Enable: AFC Mode; AFC; Frequency Offset Register                                                           'Disable: High Accuracy; Strobe
Data &H9820%                                                'OK Frequenz Shift=POS; Power Out=0 dB; Deviation=45 khz
Data &HE000%                                                'OK WakeUp-Timer=0ms
Data &HC800%                                                'OK Duty Cycle = Infinity % OFF
Data &HC000%                                                'OK Low batterie=2,2V; Clock Pin=1 Mhz
Data &HCED4%                                                'OK Synchron Pattern
Data &HCC57%                                                'PLL Settings
Data &H0000%                                                'Status lesen irqs zurückstellen
Data 0%