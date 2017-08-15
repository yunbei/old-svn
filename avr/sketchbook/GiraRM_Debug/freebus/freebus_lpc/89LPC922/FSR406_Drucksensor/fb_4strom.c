/*
 *      __________  ________________  __  _______
 *     / ____/ __ \/ ____/ ____/ __ )/ / / / ___/
 *    / /_  / /_/ / __/ / __/ / __  / / / /\__ \ 
 *   / __/ / _, _/ /___/ /___/ /_/ / /_/ /___/ / 
 *  /_/   /_/ |_/_____/_____/_____/\____//____/  
 *                                      
 *  Copyright (c) 2010 Jan Wegner
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */

// Versionen:	1.00	erste Version

	

#include <P89LPC922.h>
#include "fb_hal_lpc.h"
#include "fb_prot.h"
#include "4strom_delay.h"
#include "fb_app_4strom.h"
#include "mcp3204.h"


void main(void)
{ 
	unsigned char n;

	int th;



	restart_hw();				// Hardware zuruecksetzen
	for (n=0;n<50;n++) {
		set_timer0(0xFFFF);		// Warten bis Bus stabil
		while(!TF0);
	}
	restart_prot();				// Protokoll-relevante Parameter zuruecksetzen
	restart_app();				// Anwendungsspezifische Einstellungen zuruecksetzen


	do  {

		// ***************************************************************************
		// Hier ist der Platz f�r wiederkehrende Abfragen, die nicht zeitkritisch sind
		// ***************************************************************************


		if (!TR1)	// Nur wenn nicht gerade TR1 l�uft, also Senden/Empfangen noch nicht abgeschlossen
		{
			interrupted=0;

			th = read_strom(kanal);


//			if (!interrupted)
			if (1==1)
			{
				temp[kanal] = th;

				// Grenzwerte
				grenzwert(kanal);

				// Messwertdifferenz
				messwert(kanal);

				// Buswiederkehr bearbeiten
				if (sende_sofort_bus_return)
				{
					bus_return();
				}

				sequence=1;

				// Kanalumschaltung
				kanal++;
				kanal&=0x03;

			}

		}



		if(RTCCON>=0x80) delay_timer();	// Realtime clock Ueberlauf


		// Abfrage Programmier-Taster
		TASTER=1;					// Pin als Eingang schalten um Taster abzufragen
		if(!TASTER) {				// Taster gedr�ckt
			for(n=0;n<100;n++) {}	// Entprell-Zeit
			while(!TASTER);			// warten bis Taster losgelassen
			START_WRITECYCLE;
			WRITE_BYTE(0x00,0x60,userram[0x60] ^ 0x81);	// Prog-Bit und Parity-Bit im system_state toggeln
			STOP_WRITECYCLE;
		}
		TASTER=!(userram[0x060] & 0x01);	// LED entsprechend Prog-Bit schalten (low=LED an)
		for(n=0;n<100;n++) {}		// falls Hauptroutine keine Zeit verbraucht, der LED etwas Zeit geben, damit sie auch leuchten kann
  } while(1);
}


