/*
   phxSpeakingClock, a specking clock with the possibility of personalized and programmable
   announcements both as time and day of the week.

   (c) 2022 Guglielmo Braguglia
   Phoenix Sistemi & Automazione s.a.g.l. - Muralto - Switzerland

   Made for Seeedstudio XIAO M0 (ATSAMD21) and for WeMos ESP32 D1 Mini (ESP32)

*/

/******************************************************************************
   ANNOUNCEMENTS DEFINITIONS
 ******************************************************************************/

#ifndef ANNOUNCEMENTS
#define ANNOUNCEMENTS

/******************************************************************************
   Please note:
   ------------
   
   PROGRAM RESERVED  VALUES FROM  0 TO 69
   USER    AVAILABLE VALUES FROM 70 TO 99
 ******************************************************************************/
 
#define ANNOUNCEMENTS_VER   2

#define SONO_LE_ORE         0
#define UNA                 1
#define DUE                 2
#define TRE                 3
#define QUATTRO             4
#define CINQUE              5
#define SEI                 6
#define SETTE               7
#define OTTO                8
#define NOVE                9
#define DIECI              10
#define UNDICI             11
#define DODICI             12
#define TREDICI            13
#define QUATTORDICI        14
#define QUINDICI           15
#define SEDICI             16
#define DICIASSETTE        17
#define DICIOTTO           18
#define DICIANNOVE         19
#define VENTI              20
#define VENTUNO            21
#define VENTIDUE           22
#define VENTITRE           23
#define MEZZANOTTE         24
#define E_UN_QUARTO        25
#define E_MEZZA            26
#define E_TRE_QUARTI       27
#define E_DIECI            28
#define E_VENTI            29
#define E_TRENTA           30
#define E_QUARANTA         31
#define E_CINQUANTA        32
#define E_QUINDICI         33
#define E_QUARANTACINQUE   34

#define VENTIQUATTRO       35
#define VENTICINQUE        36
#define VENTISEI           37
#define VENTISETTE         38
#define VENTOTTO           39
#define VENTINOVE          40
#define TRENTA             41
#define TERNTUNO           42

#define GENNAIO            43
#define FEBBRAIO           44
#define MARZO              45
#define APRILE             46
#define MAGGIO             47
#define GIUGNO             48
#define LUGLIO             49
#define AGOSTO             50
#define SETTEMBRE          51
#define OTTOBRE            52
#define NOVEMBRE           53
#define DICEMBRE           54

#define DI                 55
#define DOMENICA           56
#define LUNEDI             57
#define MARTEDI            58
#define MERCOLEDI          59
#define GIOVEDI            60
#define VENERDI            61
#define SABATO             62

#define CONTROLLO_INIZIALE 63

#define PRIMO	            64
#define RESERVED_1         65
#define RESERVED_2         66
#define RESERVED_3         67
#define RESERVED_4         68

#define SILENZIO           69

/******************************************************************************
   USER VALUES
 ******************************************************************************/
 
#define IL_PRANZO          70
#define LA_CENA            71
#define LE_MEDICINE        72
#define NEGOZIO_APRE       73
#define NEGOZIO_CHIUDE     74
#define UFFICIO_APRE       75
#define UFFICIO_CHIUDE     76
#define E_RIAPRE           77

#endif
