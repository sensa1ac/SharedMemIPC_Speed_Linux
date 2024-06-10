/* 

Измерение скорость передачи данных между процессом-предком (отправитель) и процессом-потомком (получатель)
разных объёмов данных (от 2 до 20 Мбайт) через разделяемую память. 
Передаваемые данные: случайные числа от 0 до 255.

Измерение скорости происходит путём фиксации текущего времени (с 01.01.1970) в процессе-предке (сек[10] + наносек[9])
и сохранении этого времени в первые 19 элементов буфера для записи в разд.память.
Далее процесс-предок вычитывает данные из разд.памяти в свой буфер, после чего фиксирует текущее время. 
Далее находит разность времени и отображает её в терминале.

*ПРИМЕЧАНИЕ
Разд.память должна быть отсоединена в каждом процессе, а удалена только в одном.

 */

#include <sys/wait.h>
#include <sys/sem.h>
#include <sys/types.h> //этот файл заголовка содержит объявления базовых типов данных, используемых в других файлах заголовков. Например, он определяет типы данных, такие как pid_t (идентификатор процесса), uid_t (идентификатор пользователя) и gid_t (идентификатор группы).
#include <sys/ipc.h> //  этот файл заголовка содержит объявления системных вызовов и структур данных для работы с механизмом IPC (Inter-Process Communication) в операционной системе Linux. В частности, он определяет функции для создания и удаления ключей IPC, используемых для доступа к различным ресурсам IPC
#include <sys/shm.h> // этот файл заголовка содержит объявления системных вызовов и структур данных для работы с механизмом разделяемой памяти в операционной системе Linux. Он определяет функции для создания и удаления сегментов разделяемой памяти, а также функции для получения доступа к этим сегментам и управления ими.

#include <stdio.h> // содержит функции ввода-вывода, такие как printf и scanf.
#include <stdlib.h> //содержит функции для работы с памятью, такие как malloc и free, а также функции для преобразования типов данных, такие как atoi и atof.
#include <unistd.h> //содержит функции для работы с системными вызовами, такие как fork и exec.
#include <time.h> // содержит функции для работы с временем, такие как time и localtime

#define OneMbyte 1048576 // 1 МБ (1048576 байт) - начальный размер сообщения

#define SEM_KEY 1234 // ключ для семафоров

// Получает на вход по 2 значения: секунды и наносекунды (от 01.01.1970). Возвращает разность
float time_diff(int isek1, int iusek1, int isek2, int iusek2);

// Преобразование массива char в число (конкатенация всех символов-цифр). n - размер массива char
int charPointToInt (char* buf, int n);

// Выводит текущее время через два массива типа char: первый- секунды (10 символов) с 01.01.1970, второй- микросекунды (6 символов)
void get_time(char* sec, char* usec) ;

/* 
Создаёт буфер размером size байт (память выделяется динамически) и заполняет его 
числами от 0 до 255. Возвращает указатель на этот буфер
ВАЖНО (!) потом освободить буфер: "free (char* buffer);" 
*/
char* generate_buffer(int size); 


//================================================================================
int main (){	
	
	
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////	
	struct sembuf sem_op; 
	int sem_id;

    // Создаем семафоры
	
		/*В параметре доступа 0666 первая цифра (0) указывает на то, что используется восьмеричная система счисления. 
		Оставшиеся три цифры (666) определяют права доступа.
		Конкретно в данном случае:
			- Права доступа для владельца семафора установлены на чтение, запись и выполнение (6 в восьмеричной системе).
			- Права доступа для группы установлены на чтение и запись (6 в восьмеричной системе).
			- Права доступа для остальных пользователей установлены на чтение и запись (6 в восьмеричной системе).
		Таким образом, параметр доступа 0666 позволяет любому пользователю читать, записывать и использовать созданный семафор.*/
    sem_id = semget(SEM_KEY, 2, IPC_CREAT | 0666);
    if (sem_id == -1) {
        perror("semget");
        exit(1);
    }

    // Инициализируем семафоры
		// номер семафора, значение которое хотим установить
    semctl(sem_id, 0, SETVAL, 1); // семафор для родителя
    semctl(sem_id, 1, SETVAL, 0); // семафор для дочернего процесса

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////	
	
	
int pid; // для PID , PPID
int shmid; // идентификатор сегмента разделяемой памяти
char *shared_memory; // указатель на начало в сегменте

// Создание разделяемой памяти
	/*IPC_PRIVATE - это ключ, используемый для создания уникального идентификатора сегмента разделяемой памяти, 
	который не связан с другими процессами. Такой сегмент может быть использован только процессами, которые имеют 
	доступ к этому ключу*/
if ((shmid = shmget(IPC_PRIVATE, OneMbyte*20, IPC_CREAT | 0666)) == -1) {
perror("shmget");
exit(EXIT_FAILURE);
}
// Присоединение сегмента + проверка на ошибку
shared_memory = shmat(shmid, NULL, 0); 
if (shared_memory == (void*) -1) 
{
	perror("shmat");
	exit(1);
}
		
// Создание процесса-потомка + проверка на ошибку
if ((pid = fork()) < 0)
{
	perror("fork failed"); 
	exit(2);
}

////////////////////////////////////
//////////////////////////////////////////////////////
////////////////////////////////////

// ЕСЛИ СЕЙЧАС ВЫПОЛНЯЕТСЯ ПРОЦЕСС-ПРЕДОК
if (pid > 0) 
{ 


	int i = 2;  // Кол-во Мбайт
	int j=0;   // индекс для вспм цикла
	char* buf; // указатель на буфер для чтения из канала 
	char sek[10], usek[9]; // для времени сек, наносек 
	
	// В цикле передадим 10 сообщений разного объёма, ожидая прочтения потомком после каждого
	while (i <= 20)
	{

		// Создаём и заполняем буфер случайными элементами
		buf = generate_buffer(i*OneMbyte);
		
		
				// Сохраняем текущее время
		get_time(sek,usek);
		
		///usleep (200*100*1000); 
		
		// Записываем в первые 19 элментов буфера для отправки текущее значение секунд и наносекунд (10+9)
		for (j=0;j<19;j++)
		{
			if (j<10)
				buf[j] = sek[j];
			else if (j>=10)
				buf[j] = usek[j-10];
		}  
		
		///printf ("\nВ ПРЕДКЕ ДО ЗАПИСИ: sek = %s  usek = %s\n", sek,usek);

		// Запись данных в сегмент общей памяти
		snprintf(shared_memory, OneMbyte*i, "%s", buf);		
		
		///printf("===ЗАПИСЬ ПРОШЛА УСПЕШНО====\n");
		
/////////////////// Синхронизация семафорами //////////////////////////////	

			// Блокируемся до тех пор, пока дочерний процесс не прочитает данные
            sem_op.sem_num = 0; //устанавливаем номер семафора, с которым будем работать
            sem_op.sem_op = -1; //задаем операцию, которую нужно выполнить с семафором (уменьшить значение на 1)
            sem_op.sem_flg = 0; //устанавливаем флаги для операции со семафором
            semop(sem_id, &sem_op, 1); //вызываем функцию semop(), которая изменяет значение семафора с заданным номером на заданную величину

			// блокирвока снята => данные прочтены

            // Освобождаем семафор для дочернего процесса
            sem_op.sem_num = 1;
            sem_op.sem_op = 1;
            sem_op.sem_flg = 0;
            semop(sem_id, &sem_op, 1);

           /// sleep(1);
///////////////////////////////////////////////////////////////	
				
		i = i + 2;
		
		///usleep (200*100*1000);  // ждём 100мс перед отправкой новой посылки, чтоб старая наверняка была получена за это время
		free (buf);
		



	}
	wait(NULL);
			// Отсоединение сегмента
		shmdt(shared_memory); 
		
			/*удаляет сегмент разделяемой памяти с идентификатором shmid. Это означает, что после этого вызова 
		другие процессы не могут обращаться к этому сегменту памяти, и он освобождается для повторного использования.*/
		if (shmctl(shmid, IPC_RMID, NULL) == -1) {
		perror("shmctl");
		exit(EXIT_FAILURE);
		}
		/*1. shmid - идентификатор сегмента разделяемой памяти, который нужно управлять.
		2. IPC_RMID - команда для удаления сегмента разделяемой памяти.
		3. NULL - указатель на структуру, содержащую информацию о сегменте разделяемой памяти (в данном случае не используется).*/
}

////////////////////////////////////////
////////////////////////////////////////////////////////////
////////////////////////////////////////

// ЕСЛИ СЕЙЧАС ВЫПОЛНЯЕТСЯ ПРОЦЕСС-ПОТОМОК
else if (pid == 0) 
{ 

	///usleep(30*100*1000);
	
	int i = 2; // кол-во Мбайт в сообщении
	float itog_time; // искомое время передачи
	int isek2, iusek2; // время прочтения
	int isek1, iusek1; // время отправки
	int j=0; 
	char usekBuf[9]; //вспм для времени отправки	
	char* buf; // указатель на буфер для чтения из канала 
	char sek[10], usek[9]; // для времени сек, наносек 	
	
	// Входим в цикл чтения сообщений, каждое на 2 Мбайта больше предыдущего
	while (i <= 20)
	{
		///usleep (120*100*1000);
		
		// Создаём буфер для чтения
		if ( ( buf = (char*)malloc(i*OneMbyte* sizeof(char)) ) == NULL )
			{printf("exit(7);"); exit(4);}

		
///////////////////////////////////////////////////////////////	
// Блокируемся до тех пор, пока родительский процесс не запишет данные
            sem_op.sem_num = 1;
            sem_op.sem_op = -1;
            sem_op.sem_flg = 0;
            semop(sem_id, &sem_op, 1);
///////////////////////////////////////////////////////////////		
		

		// Читаем данные из сегмента общей памяти в буфер
		snprintf(buf, OneMbyte*i, "%s", shared_memory);
			
			// Манипуляции временем для получения итогового времени (time) отправки-получения 
	
			///usleep (10*100*1000);
			
			// Время получения сообщения
			get_time(sek,usek);  	
			
			// Выделение отделбьного массива наносекунд из буфера 
			for (j=10;j<19;j++)
				usekBuf[j-10] = buf[j];	
			
			//Конкатенация char в int
				// время получения
			isek2 = charPointToInt(sek,10);		
			iusek2 = charPointToInt(usek,9); 	
			
				// время отправки
			isek1 = charPointToInt(buf,10);	 // время в сек 
			iusek1 = charPointToInt(usekBuf,9); // время в наносек
			
			itog_time = time_diff(isek1, iusek1, isek2, iusek2) ;
			
		///printf ("В ПОТОМКЕ ВРЕМЯ ДО ЗАПИСИ: isek1 = %d  iusek1 = %d\n", isek1,iusek1);
		
		///printf("ПРОЧИТАНО В: строки-> sek = %s  usek = %s  int->  isek = %d  iusek = %d \n",sek,usek,isek2,iusek2); 

		printf("Сообщение № %d:		%.9f сек (%d Мбайт)\n", i/2, itog_time, (OneMbyte*i)/(1024*1024));	

		
///////////////////////////////////////////////////////////////		
// Освобождаем семафор для родительского процесса
            sem_op.sem_num = 0;
            sem_op.sem_op = 1;
            sem_op.sem_flg = 0;
            semop(sem_id, &sem_op, 1);
///////////////////////////////////////////////////////////////		
			
			
			i = i + 2;
			
			///usleep (120*100*1000);
			
			free(buf);
			///printf("Сообщение № %d\nОтправлено предком в канал: %d байт (%d Мбайт)  Получено потомком из канала: %d байт (%d Мбайт)\nВремени прошло: %.9f сек\n\n", i/2, (OneMbyte*i), (OneMbyte*i)/(1024*1024) , wasRead[i/2], wasRead[i/2]/(1024*1024), itog_time); 
			///printf("\n===ВРЕМЯ ПРИ ПОЛУЧЕНИИ====\nisek1 = %d		iusek1 = %d\niusek2 = %d		iusek2 = %d\n========\n\n",isek1, iusek1, isek2, iusek2);
		///}

		
	} // while

		 
	// Отсоединение сегмента
	shmdt(shared_memory);
	
	/*удаляет сегмент разделяемой памяти с идентификатором shmid. Это означает, что после этого вызова 
	другие процессы не могут обращаться к этому сегменту памяти, и он освобождается для повторного использования.*/
	/* if (shmctl(shmid, IPC_RMID, NULL) == -1) 
	{
		perror("shmctl");
		exit(EXIT_FAILURE);
	} */
	/*1. shmid - идентификатор сегмента разделяемой памяти, который нужно управлять.
	  2. IPC_RMID - команда для удаления сегмента разделяемой памяти.
	3. NULL - указатель на структуру, содержащую информацию о сегменте разделяемой памяти (в данном случае не используется).*/		

}	// PID
	



// Удаляем семафоры 
    semctl(sem_id, 0, IPC_RMID);

}//end
//================================================================================

/* 
Создаёт буфер размером size байт (память выделяется динамически) и заполняет его 
числами от 0 до 255. Возвращает указатель на этот буфер
ВАЖНО (!) потом освободить буфер: "free (char* buffer);" 
*/
char* generate_buffer(int size) 
{
	char* buffer;
     if ( ( buffer = (char*)malloc(size * sizeof(char)) ) == NULL )
		{printf("exit(4);"); exit(4);}
	else
	{
		
		int i;
		for (i = 19; i < size; i++) // c 19ого, т.к. элементы 0-18 будут заполнены временем
			buffer[i] = rand() % 256;	
		
	}
	//////
	/* char sek[10], usek[9]; // для времени сек,микросек
	//Сохраняем текущее время в секундах в 0-9 элементах буфера и в микросекундах в 10-15 элементах
	get_time(sek,usek);	 int j;
		for (j=0;j<19;j++)
		{
			if (j<10)
				buffer[j] = sek[j];
			else if (j>=10)
				buffer[j] = usek[j-10]; 
		} 
	printf ("\nВ ПРЕДКЕ ДО ЗАПИСИ: sek = %s  usek = %s\n", sek,usek);*/
	
    return buffer;
}

// Выводит текущее время через два массива типа char: первый- секунды с 01.01.1970, второй- наносекунды
void get_time(char* sec, char* usec) 
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    sprintf(sec, "%ld", ts.tv_sec);
    sprintf(usec, "%ld", ts.tv_nsec);
}

// Преобразование массива char в число (конкатенация всех символов-цифр). n - размер массива char
int charPointToInt (char* buf, int n)
{
    int result = 0;
    for (int i = 0; i < n; i++) {
        result = result * 10 + (buf[i] - '0');
    }
    return result;
}

// Получает на вход по 2 значения: секунды и наносекунды (от 01.01.1970). Возвращает разность
float time_diff(int isek1, int iusek1, int isek2, int iusek2) 
{
    long long usec1 = isek1 * 1000000000LL + iusek1;
    long long usec2 = isek2 * 1000000000LL + iusek2;
    return (usec2 - usec1) / 1000000000.0f;
}
/* long long (LL). LL используется для указания, что число должно быть представлено в 64-битном формате, 
что позволяет сохранить значения, превышающие максимальное значение int (2147483647).  */



