#!/usr/bin/ env python2
# -*- coding: utf-8 -*-

#!/usr/bin/ env python2
# -*- coding: utf-8 -*-


import RPi.GPIO as GPIO
import time
import math

global a
global zus
zus = ""

MISO = 21
SCK = 23
nSEL = 24
MOSI = 19

#*************************************
#Initialisierung RFM12 und GPIO (SPI)
#
#*************************************

class RFM12:

    def __init__(self,MOSI,SCK,nSEL,MISO):
        self.data = [0,0,0,0]
        self.MOSI = MOSI
        self.SCK = SCK
        self.nSEL = nSEL
        self.MISO = MISO
        GPIO.setwarnings(False)
        GPIO.setmode(GPIO.BOARD)
        GPIO.setup(self.MISO, GPIO.IN)
        self.nSEL_on()
        self.MOSI_off()
        self.SCK_off()
        time.sleep(0.12)
        self.Write_CMD(0x80D7)
        self.Write_CMD(0x82D9)
        self.Write_CMD(0xA67C)
        self.Write_CMD(0xC623)
        self.Write_CMD(0x90C0)
        self.Write_CMD(0xC2AC)
        self.Write_CMD(0xCA80)
        self.Write_CMD(0xC483)
        self.Write_CMD(0x9820)
        self.Write_CMD(0xE000)
        self.Write_CMD(0xC800)
        self.Write_CMD(0xC000)
        self.Write_CMD(0xCED4)
        self.Write_CMD(0xCC57)
        self.Write_CMD(0x0000)

    def nSEL_on(self):
        GPIO.setup(self.nSEL, GPIO.OUT)
        GPIO.output(self.nSEL, GPIO.HIGH)

    def nSEL_off(self):
        GPIO.setup(self.nSEL, GPIO.OUT)
        GPIO.output(self.nSEL, GPIO.LOW)
        
    def SCK_on(self):
        GPIO.setup(self.SCK, GPIO.OUT)
        GPIO.output(self.SCK, GPIO.HIGH)

    def SCK_off(self):
        GPIO.setup(self.SCK, GPIO.OUT)
        GPIO.output(self.SCK, GPIO.LOW)    
        
    def MOSI_on(self):
        GPIO.setup(self.MOSI, GPIO.OUT)
        GPIO.output(self.MOSI, GPIO.HIGH)

    def MOSI_off(self):
        GPIO.setup(self.MOSI, GPIO.OUT)
        GPIO.output(self.MOSI, GPIO.LOW)

    def In_MISO(self):
        return(GPIO.input(self.MISO))


#************************************************
#Schreib- Befehl zum RFM12
#
#************************************************

    def Write_CMD(self, CMD):
        temp = 0
        self.SCK_off
        self.nSEL_off()
        for i in range(0, 16, 1):
            if CMD&0x8000:
                self.MOSI_on()
            else:
                self.MOSI_off()
            CMD<<=1
            temp<<=1
            if self.In_MISO() == True:
                temp|=0x0001
            self.SCK_on()
            self.SCK_off()
        self.MOSI_off()
        self.nSEL_on()
        return(temp)

#************************************************
#Pollen auf Empfang
#
#************************************************


    def Wait_Ready(self):
        self.nSEL_off()
        a = 0
        b = 0
        while self.In_MISO() == 0:
            time.sleep(0.00)

#************************************************
#RFM12 auslesen
#
#************************************************

    def Read(self, data, size):
        self.Write_CMD(0x82C8)
        self.Write_CMD(0xCA81)
        self.Write_CMD(0xCA83)
        self.MOSI_off()
        for i in range(0, size, 1):
            self.Wait_Ready()
            data[i] = self.Write_CMD(0xB000)& 0x0FF
        self.Write_CMD(0x8208)
        return data
            

if __name__== "__main__":
    rfm = RFM12(MOSI, SCK, nSEL, MISO)

    while(1):

        #data = ""        
        a = rfm.Read(rfm.data, 4)
        for i in range(0, 4, 1):

             xy = chr(int(a[i]))
             zus = zus + xy
             xy = ""
             if i == 3:
                 print (zus)
                 time.sleep (.5)
                 i = 0
                 zus = ""
            
        




















