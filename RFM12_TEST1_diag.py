#!/usr/bin/ env python3
# -*- coding: utf-8 -*-
import classrfm12
from classrfm12 import RFM12
import time
from time import sleep
import sqlite3

import serial
import locale
import pytz
from datetime import datetime
from pytz import timezone


import sys
from decimal import Decimal
import os.path


tz = pytz.timezone('Europe/Berlin')





MISO = 21
SCK = 23
nSEL = 24
MOSI = 19

xy =""
zus = ""

def holdatas():
        xy =""
        zus = ""
        rfm = RFM12(MOSI, SCK, nSEL, MISO)
        while(1):
        
                a = rfm.Read(rfm.data, 4)
                for i in range(0, 4, 1):

                     xy = chr(int(a[i]))
                     zus = zus + xy
                     xy = ""
                     if i == 3:
                         i = 0
                         data = zus

                         zus = ("")

                print(data)
                return data

def protokollcheck(alarmdata):

        if alarmdata =="nixx" or alarmdata =="ALVO" or alarmdata =="ALHI":
                return True

def writetolog(logdaten):


        with open("//home/pi/Öffentlich/alarm.txt", "a" ) as fh:                          

                        fh.write(logdaten+ "\r\n")

                        fh.close
              

def hauptprogramm():
        cntalvo = 0
        cntalhi = 0


        try:
                with open("//home/pi/Öffentlich/alarm.db") : pass
                print("Datenbankdatei gefunden: " + "//home/pi/Öffentlich/alarm.db")

        
        except IOError:
                print ("Ich erzeuge eine neue Datenbank: " + "//home/pi/Öffentlich/alarm.db")
                
                time.sleep(1)


        connection = sqlite3.connect("//home/pi/Öffentlich/alarm.db")
        cursor = connection.cursor()
        cursor.execute('''CREATE TABLE IF NOT EXISTS alarm (RowID INTEGER PRIMARY KEY, 
                                               Datum DATE,
                                               Zeit VARCHAR(10),
                                               WO VARCHAR(10),
                                               Dauer VARCHAR (4))''')

        while(True):
                stunde = datetime.now(timezone('Europe/Berlin'))
                datum = time.strftime('%d %m %Y')
                zeit = time.strftime('%H:%M:%S')
                stund = time.strftime("%H")
                timestr = time.strftime("%Y%m%d")          
                minut = time.strftime("%M")
                jahr = time.strftime ("%Y")     
                sekund = time.strftime("%S")
                alarmdata = holdatas()
                
                if protokollcheck(alarmdata) == True:
                        if alarmdata =="ALVO" :
                                cntalvo +=1

                        elif alarmdata == "ALHI":
                                cntalhi +=1

                        elif alarmdata == "nixx":
                                if cntalvo !=0:
                                        logtext = "Am : "+ datum + "  um  " + zeit + " Uhr" + "  Alarm vorne ; Dauer = " + str(int(cntalvo/3)) + " sec"
                                        writetolog(logtext)
                                        print(logtext)
                                        sql_command = '''INSERT INTO wetter VALUES(NULL, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)''' 
                                        cursor.execute(sql_command, (datum, zeit, "vorne", str(int(cntalvo/3)) + " sec"))
                                        connection.commit()
                                elif cntalhi !=0:
                                        logtext = "Am : "+ datum + "  um  " + zeit + " Uhr" + "  Alarm hinten ; Dauer = " + str(int(cntalhi/3)) + " sec"
                                        writetolog(logtext)
                                        print(logtext)
                                        sql_command = '''INSERT INTO wetter VALUES(NULL, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)''' 
                                        cursor.execute(sql_command, (datum, zeit, "hinten", str(int(cntalhi/3)) + " sec"))
                                        connection.commit()

                                cntalhi = 0
                                cntalvo = 0


                        #print("Alarmdata = ", alarmdata, cntalvo)
                        #sleep(1)

hauptprogramm()


