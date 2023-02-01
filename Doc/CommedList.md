**SETDT,YY,MM,GG,HH,MN,SS,DW**

Set date/time

	YY : Year (0 .. 99)
	
	MM : Month (1 .. 12)
	
	GG : Day (1 .. 31)
	
	HH : Hours (0 .. 23)
	
	MN : Minutes (0 .. 59)
	
	SS : Seconds (0 .. 59)
	
	DW : Day of Week (1 .. 7, 1 = Sunday)

*note: You **MUST** always set the "**standard time**" and never the "daylight saving time", even when it is active. This means that during "daylight saving time", you must provide the date/time as it would be if "standard time" were in effect.* *The watch corrects itself and displays the correct time.*


**GETDT**

Get (*print*) date/time in the same format of SETDT (*valid only from serial port*)


**SETUD,DS**

Set use DST

	DS : 1 = use DST, 0 = Don't use DST


**SETDB,DS,DN**

Set display brightness

	DS : display brightness during day (0 .. 15)
	
	DN : display brightness during night (0 .. 15)


**SETPA,MN**

Set period of announcements

	MN : can be only 10, 15, 30 or 60 minutes


**SETON,HH,MN**

Set audio ON time for announcements

	HH : Hours (0 .. 23)
	
	MN : Minutes (0 .. 59)


**SETOF,HH,MN**

Set audio OFF time for announcements

	HH : Hours (0 .. 23)
	
	MN : Minutes (0 .. 59)


**SETAM,MN,VD,HO,MO,HF,MF,M1,M2,...** *up to max of (MAX\_USR\_LNG - 1) messages*

Set User defined announcement (*played after the standard announcement*)

	MN : Message Number (0 .. MAX\_USR\_MSG - 1)
	
	VD : Valid days (0 = all, 1 .. 254 days selection, *see note 1*)
	
	HO : Hours Start (0 .. 23)
	
	MO : Minutes Start (0 .. 59)
	
	HF : Hours Stop (0 .. 23)
	
	MF : Minutes Stop (0 .. 59)
	
	Mn : Audio message number (ZH/nn.MP3), can be repeated max (MAX\_USR\_LNG - 1) times

*note 1: VD is a binary field, valid bit are from  bit 1 to bit 7, bit zero is reserved. To*

*indicate valid days sum the decimal value of each bit. So, Sun = 2, Mon = 4; Tue 8,*

*Wed = 16, Thu = 32, Fri = 64, Sat = 128. Eg. to be valid only for Mon, Wed, Fri you*

*have to sum 4 + 16 + 64 = 84 which is the value to enter in the VD field.*


**CLRAM,MN**

Clear User defined announcement (*played after the standard announcement*)

	MN : Message Number (0 .. MAX\_USR\_MSG)


**GETPM**

Get (print) all parameters (*valid only from serial port*)




**SETOD,WD**

Set audio off week day

	WD : from 0 to 7 (*1 = Sunday, 7 = Saturday, 0 = all days on*)


**SETAV,VL**

Set audio volume

	VL : from 0 to 30


**SETDY,UD**

Set adds the date in the time announcement

	UD : 1 = adds, 0 = Don't adds


**RESET**

Do a software reset of the MCU and restart the program
