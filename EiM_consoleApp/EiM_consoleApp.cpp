// EiM_consoleApp.cpp : This file contains the 'main' function. Program execution begins and ends there.


//  E1097
#define no_init_all    // dla VS < 2019

#include "pch.h"
#include <iostream>
#include <fstream>
#include <windows.h>
#include <filesystem>

using namespace std;

/*******************************************************************************************/
/******************************* Wybór formatu pliku CSV ***********************************/
/*******************************************************************************************/
//*  USE_POLISH_CSV  0    =>    format ogólnoswiatowy:  3.14, 3.14, 3.14, 
//*  USE_POLISH_CSV  1    =>    format polski        :  3,14; 3,14; 3,14; 


#define  USE_POLISH_CSV  0

/********************************************************************************************/
/********************************************************************************************/
/***********************  KOMENDY TEKSTOWE DLA TESTERA    ***********************************/
/********************************************************************************************/
/********************************************************************************************/

//"VOLT1 3.141\n"       - ustawienie napiêcia SEM1 w[ V ]
//"MEAS:CURR? (@1)\n"   - pomiar pr¹du w kan. 1, czyli SEM1

//"VOLT2 6.284\n"       - ustawienie napiêcia SEM2 w[ V ]
//"MEAS:CURR? (@2)\n"   - pomiar pr¹du w kan. 2, czyli SEM2

//"CURR 2.721\n"        - ustawienie pr¹du SPM w[ mA ]
//"MEAS:VOLT? (@3)\n"   - pomiar napiêcia w kan. 3, czyli na SPM

//"MEAS:VOLT? (@4)\n"   - pomiar napiêcia w kan. 4, czyli wej AUX

/********************************************************************************************/
/********************************************************************************************/


/**
 * @fn HANDLE openSerialPort( char* portName ) 
 * @brief  funkcja otwieraj¹ca port szeregowy z w³aœciwymi parametrami do obs³ugi testera
 *
 *     W razie nie otwarcia portu funkcja koñczy dzia³anie programu i wypisuje w konsoli odpowiedni komunikat.
 *
 * @param portName  - ³añcuch znakowy z nazw¹ pliku, np. "COM1", "COM47"
 *                    w systemie Windows trzeba podejrzeæ w Mened¿erze urz¹dzeñ / Porty COM i LPT (?) 
 *                    jaki numer zosta³ nadany wirtualnemu portowi szeregowemu dla u¿ytego adaptera USB/Uart.
 *
 * @retval HANDLE   - zwraca "uchwyt" (identyfikator) otwartego portu szeregowego, który nalezy u¿ywaæ 
 *                    do zapisywania i odczytywania tego portu szeregowego
 */
HANDLE openSerialPort( char* portName ) 
{
	HANDLE _hSerial = CreateFileA( portName, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0 );
	if( _hSerial == INVALID_HANDLE_VALUE )
	{
		DWORD err = GetLastError();
		if( err == ERROR_FILE_NOT_FOUND )
		{
			std::cout << "Serial port not found.\n";
		}
		else
		{
			std::cout << "Serial port not opened. Error no " << err << "\n";
		}
		exit( 1 );
	}
	PurgeComm( _hSerial, PURGE_TXCLEAR | PURGE_RXCLEAR );

	// Do some basic settings
	DCB _serialParams = { 0 };
	_serialParams.DCBlength = sizeof( _serialParams );

	GetCommState( _hSerial, &_serialParams );
	_serialParams.BaudRate = CBR_115200; // 115200;
	_serialParams.ByteSize = 8;
	_serialParams.StopBits = ONESTOPBIT; //  1;
	_serialParams.Parity = NOPARITY;  // 0;
	if( !SetCommState( _hSerial, &_serialParams ) )
	{
		std::cout << "Could not set serial port.\n";
		exit( 2 );
	}

	// Set timeouts
	COMMTIMEOUTS _timeout = { 0 };
	_timeout.ReadIntervalTimeout = 50;
	_timeout.ReadTotalTimeoutConstant = 250;
	_timeout.ReadTotalTimeoutMultiplier = 10;
	_timeout.WriteTotalTimeoutConstant = 50;
	_timeout.WriteTotalTimeoutMultiplier = 10;

	SetCommTimeouts( _hSerial, &_timeout );

    std::cout << "Serial port opened!\n"; 
	return _hSerial;
}

/**
 * @fn writeOrder( HANDLE, const char * )
 * @brief  funkcja, która wysy³a poprzez (otwarty wczeœniej) port szeregowy wskazywany przez 'hPort'
           komendê steruj¹c¹ do testera zapisan¹ w ³añcuchu znakowym 'order'
 *
 * @param hPort  - "uchwyt" (identyfikator) portu szeregowego do którego chcemy wys³aæ komendê
 * @param order  - wskaŸnik do ³añcucha znakowego zawieraj¹cego komendê 
 * @retval brak 
 */
void writeOrder( HANDLE hPort, const char *order )
{
	DWORD dummyArg;
	char  dummyBuf[ 50 ];
	WriteFile( hPort, order, strlen( order ), &dummyArg, NULL );
	//ReadFile( hPort, dummyBuf, sizeof( dummyBuf )-1, &dummyArg, NULL );
	PurgeComm( hPort, PURGE_RXCLEAR );
}

/**
 * @fn double getMeasValue( HANDLE, int, double, const char*, double )
 * @brief  funkcja odczytuj¹ca wartoœæ napiêcia lub pr¹du z zadan¹ precyzj¹ 
 *         zwraca odczytan¹ wartoœæ dopiero gdy  odczyt siê ustabilizuje lub po wyczerpaniu limitu odczytów
 *
 * @param hPort  - "uchwyt" (identyfikator) portu szeregowego do którego chcemy wys³aæ komendê
 * @param n_max  - limit odczytów - maksymalna liczba odczytów i czekania na ustabilizowanie pomiarów
 * @param t_delay  - czas w sekundach pomiêdzy kolejnymi odczytami
 * @param order  - wskaŸnik do ³añcucha znakowego zawieraj¹cego komendê co odczytywaæ
 * @param prec  - ¿¹dana precyzja ustabilizowania siê pomiaru w woltach lub miliamperach (?)
 * @retval      - zwraca ostatni¹ odczytan¹ wartoœæ
 */
double getMeasValue( HANDLE hPort, int n_max, double t_delay, const char* order, double prec )
{
//	_startTime = time.time()
	int	   _n = 0;
	double _meas = 0;     // wartoœæ aktualnego pomiaru
	double _pMeas = 0;    // wartoœæ poprzedniego pomiaru

	while( _n < n_max )   
	{
		//time.sleep( t_delay )
		Sleep( (DWORD)( 1000 * t_delay) );

		writeOrder( hPort, order );   // wys³anie komendy z ¿¹daniem odczytu pomairu pr¹du lub napiêcia
		_pMeas = _meas;

		DWORD dummyArg;        // nieu¿ywana zmienna, która jest niezbêdna do wywo³¹nia funkcji ReadFile (biblioteczna funkcja)
		char  inputBuf[ 50 ];  // bufor do którego wpisane zostan¹ znaki odebrane z testera
		if( !ReadFile( hPort, inputBuf, sizeof( inputBuf ) - 1, &dummyArg, NULL ) )  // próba odczytu z portu szeregowego odpowiedzi testera
		{
			std::cout << "  --read error\n";
		}
		sscanf_s( inputBuf, "%lf", &_meas );  // konwersja z ASCII na double

		std::cout << "    --- n = " << _n << " meas = " << _meas << "\n";

		if( abs( _pMeas - _meas ) < prec )  // sprawdzenie czy pomiar jest ustabilizowany
		{
			std::cout << "  --prec achieved\n";
			break;
		}
		_n = _n + 1;
	}
	if( _n == n_max )
	{
		std::cout << "  --timeout achieved\n";   // komunikat o przekroczeniu liczby dopuszczalnych odczytów i NIE osi¹gniêciu ¿¹danej precyzji
		//_endTime = time.time()
		//print( "  --time elpsed = %s" % ( _endTime - _startTime ) )
	}
	return _meas;
}

#if USE_POLISH_CSV

std::ofstream&  operator << ( std::ofstream& outFile, double &val )
{
	char _aux[ 20 ];
	sprintf_s( _aux, "%0.5f", val );
	char *ret = strchr( _aux, '.' );
	
	if( ret != NULL )
	{
		*ret = ',';
	}
	outFile <<  _aux;
	return outFile;
}

const char separator = ';';

#else

const char separator = ',';

#endif
 
int main()
{
	/********************************************************************************************/
	/************************  Zbieranie serii pomiarowych    ***********************************/
	/********************************************************************************************/

	HANDLE hSerial = openSerialPort( (char *)"COM1" );      // otwarcie poru szeregowego

	writeOrder( hSerial, "VOLT1 4.773\n" );     // wys³anie testowej komendy - WYKONAJ NA POCZ¥TKU LABORATORIUM KROKOWO, ¿eby mieæ pwenoœæ, ¿e komunikacja dzia³a


	const int  noOfCurves = 2;

	


	double     setValues[] = { 0.002, 0.005, 0.001, 0.015, 0.02, 0.03, 0.04, 0.05, 0.06, 0.07, 0.08, 0.09, 0.1};
	// Wartoœci Pr¹dów do tranzystora // tablica z wartoœciami ustawianymi do pomiaru pojedynczej charakterystki


	/*double     setValues[] = { 0.00, 0.05, 0.01, 0.015, 0.02, 0.03, 0.04, 0.05, 0.06, 0.09, 1, 2, 3,4,5,6,7,8,9,10};*/
	// Wartoœci Napiêæ do tranzystora // tablica z wartoœciami ustawianymi do pomiaru pojedynczej charakterystki


	const int  noOfPoints = sizeof( setValues ) / sizeof( double );      // liczba wartoœci ustawianych

	double     measValues[ noOfCurves ][ noOfPoints ];      // tablica dwuwymiarowa do zapamiêtania pomiarów wielkoœci nr 1
	std::cout << "Starting measurements" << std::endl;

	char  order[ 30 ]; //miejsce przeznaczone na polecenie


	

		for( int i = 0; i < noOfPoints; i++ )                     // zebranie pojedynczej charakterystyki
		{
//		
			sprintf_s( order, "CURR %f\n", setValues[ i ] );      
			//  ustawienie pr¹du (ustawienie wartoœci z tablicy; index = i)



			sprintf_s( order, "VOLT1 %f\n", setValues[ i ] );      
			//  ustawienie napiêcia (ustawienie wartoœci z tablicy; index = i)
			writeOrder( hSerial, order );                          // ustawienie wartoœci pr¹du lub napiêcia
			// Dostêpne VOLT1, VOLT2 i CURR


			Sleep( 1000 );                                         // odczekanie na ustabilizowanie siê Ÿród³a
			// odczyt wartoœci mierzonej nr 1 (1 - U1, 2 - U2, 3 - I)
			double meas = getMeasValue( hSerial, 40, 0.1, "MEAS:VOLT1? (@1)\n", 0.0001 );    
			double meas2 = getMeasValue( hSerial, 40, 0.1, "MEAS:CURR? (@3)\n", 0.0001 );    

			measValues[ 0 ][ i ] = meas;
			measValues[ 1 ][ i ] = meas2;

			std::cout<< "*************************************************" << std::endl;
			std::cout<< "Wykonano dla I : " << setValues[i] << " mA | wynik pomiaru: " << meas << " : " << meas2 << " : " << std::endl;
			std::cout<<"*************************************************" << std::endl;
			

		}
	

	CloseHandle( hSerial );       // zamkniêcie portu szeregowego 


	/********************************************************************************************/
	/************************  Zapis zebranych danych do pliku  *********************************/
	/********************************************************************************************/

	std::string  tempFileName = "pomiary.csv";    // TODO // nazwa pliku fo którego wpisane zostan¹ pomiary
	std::ofstream  outFile( tempFileName );   // otwarcie pliku

	std::cout << "file opened";

	int  i, j = 0;

	outFile << "Plik pomiarowy";       // TODO // wiersz nag³ówka z opisem co jest w pliku

	for( i = 0; i < noOfPoints; i++ )         // wiersz 
	{
		outFile << separator;
		outFile << setValues[ i ];
	}
	outFile << endl;

	for( j = 0; j < noOfCurves; j++ )
	{
		outFile << " ";
		for( i = 0; i < noOfPoints; i++ )
		{
			outFile << separator;
			outFile << measValues[ 0 ][ i ]; //Wypisanie do pliku pomiaru nr 1
			outFile << separator;
			outFile << measValues[ 1 ][i]; //Wypisanie do pliku pomiaru nr 2
		}
		outFile << endl;
	}
	outFile.close();

	std::cout << "All done!";
}
