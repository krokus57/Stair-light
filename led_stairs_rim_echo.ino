#include "Adafruit_WS2801.h"
#include "SPI.h" // Comment out this line if using Trinket or Gemma
#ifdef __AVR_ATtiny85__
 #include <avr/power.h>
#endif

//================================================
//================================================
#define stairsCount 14          //количество ступенек
byte ledsinBlock = 7; //количество диодов на ступеньку

int deltaStair = 250; //   Время на одну ступень в целом  
int deltaBrightness = 50; // Время на одно увеличение яркости

#define waitForTurnOff 15000     //мс, время задержки подсветки во вкл состоянии после включения последней ступеньки
#define sensorInactiveTime 3000 //мс, время после срабатывания сенвора, в течение которого сенсор игнорит следующие срабатывания

#define sensorType 2             //1=SRF05 сонар  2=PIR сенсоры

#define pir1SignalPin 8         //не менять, если не уверены на 100%, что ИМЕННО делаете!
#define pir2SignalPin 6         //не менять, если не уверены на 100%, что ИМЕННО делаете!

uint8_t dataPin  = 11;    // Yellow wire on Adafruit Pixels
uint8_t clockPin = 13;    // Green wire on Adafruit Pixels

#define DEBUG // Вывод отладочной информации

//================================================
//================================================

#ifdef DEBUG
  #include <advancedSerial.h>
#endif

byte direction = 0;             //0 = снизу вверх, 1 = сверху вниз

byte stairsArray[stairsCount];  //массив, где каждый элемент соответствует ступеньке. Все операции только с ним, далее sync2realLife
byte stairsToProcess[stairsCount];  //массив, где каждый элемент соответствует ступеньке. Все операции только с ним, далее sync2realLife

unsigned long prevStairTime = 0; //Таймер увеличения ступеней        
unsigned long prevBrigtness = 0; //Таймер увеличения яркостей


byte ignoreSensor1Count = 0;     //счетчик-флаг, сколько раз игнорировать срабатывание сенсора 1
byte ignoreSensor2Count = 0;     //счетчик-флаг, сколько раз игнорировать срабатывание сенсора 2
boolean sensor1trigged = false;  //флаг, участвующий в реакциях на срабатывание сенсора в разных условиях
boolean sensor2trigged = false;  //флаг, участвующий в реакциях на срабатывание сенсора в разных условиях

boolean allLEDsAreOn = false;           //флаг, все светодиоды включены, требуется ожидание в таком состоянии
boolean needToLightOnBottomTop = false;  //флаг, требуется включение ступеней снизу вверх
boolean need2LightOffBottomTop = false; //флаг, требуется выключение ступеней снизу вверх
boolean needToLightOnTopBottom = false;  //флаг, требуется включение ступеней сверху вниз
boolean need2LightOffTopBottom = false; //флаг, требуется выключение ступеней сверху вниз
boolean nothingHappening = true;        //флаг, указывающий на "дежурный" режим лестницы, т.н. исходное состояние

byte currStair=0;

unsigned long sensor1previousTime;       //время начала блокировки сенсора 1 на sensorInactiveTime миллисекунд
unsigned long sensor2previousTime;       //время начала блокировки сенсора 2 на sensorInactiveTime миллисекунд
unsigned long allLEDsAreOnTime;         //время начала горения ВСЕХ ступенек
unsigned long prevRedrawTime;           //так как пикселы периодически глючат, раз в минуту перерисовываем все 

bool force_trigger_1 = false;
bool force_trigger_2 = false;
byte currBrightness=0;

unsigned long prevLiveTime=0;
unsigned long prevDebugTime=0;

#define sonar1minLimit 30       //см, если обнаружена дистанция меньше, чем это число, то сонар считается сработавшим
#define sonar2minLimit 30       //см, если обнаружена дистанция меньше, чем это число, то сонар считается сработавшим

#define useResetMechanism false   //использовать ли механизм reset (имеет смысл только при использовании СОНАРОВ)
#define sonar1trig 8            //имеет смысл только при использовании СОНАРОВ
#define sonar1echo 2            //имеет смысл только при использовании СОНАРОВ
#define sonar1resetpin 4        //имеет смысл только при использовании СОНАРОВ
#define sonar2echo 7            //имеет смысл только при использовании СОНАРОВ
#define sonar2trig 6            //имеет смысл только при использовании СОНАРОВ
#define sonar2resetpin 5        //имеет смысл только при использовании СОНАРОВ


bool needToPrintStair=false; 
bool needToPrintBrightness=false; 

Adafruit_WS2801 strip = Adafruit_WS2801(stairsCount*ledsinBlock, dataPin, clockPin);

void setup(){   //подготовка

  #ifdef DEBUG
    Serial.begin(115200);
    aSerial.setPrinter(Serial);
  #endif
  
  strip.begin();
  debug("Starting");  

  for (int i = 0; i < stairsCount; i++) 
  {
    stairsArray[i] = 0; //забить массив начальными значениями яркости ступенек
    stairsToProcess[i] = 0;
  }

  sync2RealLife();   //"пропихнуть" начальные значения яркости ступенек в "реальную жизнь"

  sensorPrepare(1);  //подготавливаем сенсора 1
  sensorPrepare(2);  //подготавливаем сенсора 2
  
}




void loop(){//бесконечный цикл

  //индикация подачи питания
  //индикация каждые 5 минут
  //бегущие быстро крайние огни и медленнее остальные
    
  sensor1trigged = sensorTrigged(1);    //выставление флага сонара 1 для последующих манипуляций с ним
  sensor2trigged = sensorTrigged(2);    //выставление флага сонара 2 для последующих манипуляций с ним
   
  nothingHappening = !((needToLightOnTopBottom)||(need2LightOffTopBottom)||(needToLightOnBottomTop)||(need2LightOffBottomTop)||(allLEDsAreOn));
  
  if (nothingHappening) //если лестница находится в исходном (выключенном) состоянии, можно сбросить флаги-"потеряшки" на всякий случай
  {               
    ignoreSensor1Count = 0;            //сколько раз игнорировать сенсора 1
    ignoreSensor2Count = 0;            //сколько раз игнорировать сенсора 2
  }

 unsigned long currRedrawTime = millis();
  if (currRedrawTime - prevRedrawTime >= 60000) 
  {
    prevRedrawTime = currRedrawTime;
    
    //так как пикселы периодически глючат, раз в минуту перерисовываем все 
    if (nothingHappening)       
    {
      debug("Force redraw");
      //clear1(0, strip.numPixels(), 0, 0, 0);
      sync2RealLife();
    }
  } 
 
  unsigned long currDebugTime = millis();
  if (currDebugTime - prevDebugTime >= 500) 
  {
    prevDebugTime = currDebugTime;
    
    if (needToLightOnTopBottom) debug("needToLightOnTopBottom");
    if (need2LightOffTopBottom) debug("need2LightOffTopBottom");
    if (needToLightOnBottomTop) debug("needToLightOnBottomTop");
    if (need2LightOffBottomTop) debug("need2LightOffBottomTop");  
    if (!sensorEnabled(1)) debug("sensor 1 Disabled");
    if (!sensorEnabled(2)) debug("sensor 2 Disabled");

    if (allLEDsAreOn) debug("allLEDsAreOn");
  } 
  
  //процесс включения относительно сложен, нужно проверять кучу условий
  //процесс ВКЛючения: сначала - снизу вверх (выставление флагов и счетчиков)
  if ((sensor1trigged) && (nothingHappening)) //простое включение ступенек снизу вверх из исходного состояния лестницы
  {
    needToLightOnBottomTop = true;              //начать освение ступенек снизу вверх
    ignoreSensor2Count++;                      //игнорить противоположный сенсора, чтобы при его срабатывании не запустилось "загорание" сверху вниз
    prevStairTime=0;
    currStair=0;
    debug("\n==On Bottom => Top ");

    stairsToProcess[0]=1; //Начать с первой ступени
    for (int i = 1; i < stairsCount; i++) stairsToProcess[i] = 0;
  }
  else 
    if ((sensor1trigged) && ((needToLightOnBottomTop)||(allLEDsAreOn))) //если ступеньки уже загоряются в нужном направлении или уже горят
    {
      sensorDisable(1);                          //просто увеличить время ожидания полностью включенной лестницы снизу вверх
      allLEDsAreOnTime = millis();
      ignoreSensor2Count++;                      //игнорить противоположный сенсора, чтобы при его срабатывании не запустилось "загорание" сверху вниз
      direction = 0;                             //направление - снизу вверх
    }
    else 
      if ((sensor1trigged) && (need2LightOffBottomTop))  //а уже происходит гашение снизу вверх
      {
        need2LightOffBottomTop = false;            //прекратить гашение ступенек снизу вверх
        needToLightOnBottomTop = true;             //начать освещение ступенек снизу вверх
        ignoreSensor2Count++;                      //игнорить противоположный сенсора, чтобы при его срабатывании не запустилось "загорание" сверху вниз
        stairsToProcess[0]=1;                      //Начать с первой ступени
        for (int i = 1; i < stairsCount; i++) stairsToProcess[i] = 0;

      }
      else 
        if ((sensor1trigged) && (needToLightOnTopBottom))   //а уже происходит освещение сверху вниз
        {
          needToLightOnTopBottom = false;             //прекратить освещение ступенек сверху вниз
          needToLightOnBottomTop = true;              //начать освение ступенек снизу вверх
          ignoreSensor2Count++;                       //игнорить противоположный сенсора, чтобы при его срабатывании не запустилось "загорание" сверху вниз
          stairsToProcess[0]=1;                       //Начать с первой ступени
          for (int i = 1; i < stairsCount; i++) stairsToProcess[i] = 0;
          
        }
        else 
          if ((sensor1trigged) && (need2LightOffTopBottom))  //а уже происходит гашение сверху вниз
          {
            need2LightOffTopBottom = false;            //прекратить гашение ступенек сверху вниз
            needToLightOnBottomTop = true;             //начать освение ступенек снизу вверх
            ignoreSensor2Count++;                      //игнорить противоположный сенсора, чтобы при его срабатывании не запустилось "загорание" сверху вниз
            stairsToProcess[0]=1;                      //Начать с первой ступени
            for (int i = 1; i < stairsCount; i++) stairsToProcess[i] = 0;
          }
      
  //процесс ВКЛючения: теперь - сверху вниз (выставление флагов и счетчиков)
  
  if ((sensor2trigged) && (nothingHappening)) //простое включение ступенек сверху вниз из исходного состояния лестницы
  {
    needToLightOnTopBottom = true;              //начать освещение ступенек сверху вниз
    ignoreSensor1Count++;                      //игнорить противоположный сенсора, чтобы при его срабатывании не запустилось "загорание" снизу вверх
    prevStairTime=0;
    currStair=stairsCount-1;
    debug("\n==On Top => Bottom ");

    stairsToProcess[stairsCount-1]=1; //Начать с первой ступени
    for (int i = 0; i < stairsCount-1; i++) stairsToProcess[i] = 0;
  }
  else 
    if ((sensor2trigged) && ((needToLightOnTopBottom)||(allLEDsAreOn)))//если ступеньки уже загоряются в нужном направлении или уже горят
    {
      sensorDisable(2);                          //обновить отсчет времени для освещения ступенек сверху вниз
      allLEDsAreOnTime = millis();
      ignoreSensor1Count++;                      //игнорить противоположный сенсора, чтобы при его срабатывании не запустилось "загорание" снизу вверх
      direction = 1;                             //направление - сверху вниз
    }
  else 
    if ((sensor2trigged) && (need2LightOffTopBottom))  //а уже происходит гашение сверху вниз
    {
      need2LightOffTopBottom = false;            //прекратить гашение ступенек сверху вниз
      needToLightOnTopBottom = true;              //начать освещение ступенек сверху вниз
      ignoreSensor1Count++;                      //игнорить противоположный сенсора, чтобы при его срабатывании не запустилось "загорание" снизу вверх
      currStair=stairsCount-1;
      stairsToProcess[stairsCount-1]=1; //Начать с первой ступени
      for (int i = 0; i < stairsCount-1; i++) stairsToProcess[i] = 0;
    }
    else 
      if ((sensor2trigged) && (needToLightOnBottomTop))   //а уже происходит освещение снизу вверх
      {
        needToLightOnBottomTop = false;             //прекратить освещение ступенек снизу вверх
        needToLightOnTopBottom = true;              //начать освение ступенек сверху вних
        ignoreSensor1Count++;                      //игнорить противоположный сенсора, чтобы при его срабатывании не запустилось "загорание" снизу вверх
        currStair=stairsCount-1;
        stairsToProcess[stairsCount-1]=1; //Начать с первой ступени
        for (int i = 0; i < stairsCount-1; i++) stairsToProcess[i] = 0;
      }
      else 
        if ((sensor2trigged) && (need2LightOffBottomTop))  //а уже происходит гашение снизу вверх
        {
          need2LightOffBottomTop = false;            //прекратить гашение ступенек снизу вверх
          needToLightOnTopBottom = true;              //начать освение ступенек сверху вниз
          ignoreSensor1Count++;                      //игнорить противоположный сенсора, чтобы при его срабатывании не запустилось "загорание" снизу вверх
          currStair=stairsCount-1;
          stairsToProcess[stairsCount-1]=1; //Начать с первой ступени
          for (int i = 0; i < stairsCount-1; i++) stairsToProcess[i] = 0;
        }
    
  //процесс ВЫКлючения относительно прост - нужно только знать направление, и выставлять флаги
  if ((allLEDsAreOn) && ((allLEDsAreOnTime + waitForTurnOff) <= millis())) //пора гасить ступеньки в указанном направлении
  {
    if (direction == 0) 
    {
      need2LightOffBottomTop = true;        //снизу вверх
      debug("\nOff Bottom => Top");
      prevStairTime=0;
      currStair=0;

      stairsToProcess[0]=1; //Начать с первой ступени
      for (int i = 1; i < stairsCount; i++) stairsToProcess[i] = 0;    
    }
    else 
      if (direction == 1) 
      {
        need2LightOffTopBottom = true;   //сверху вниз
        debug("\nOff Top => Bottom");
        prevStairTime=0;
        currStair=stairsCount-1;

        stairsToProcess[currStair]=1; 
        for (int i = 0; i < stairsCount-1; i++) stairsToProcess[i] = 0;            
      }
  }

  if (needToLightOnBottomTop)
  {     
    startBottomTop();
    sync2RealLife();
  }  

  if (need2LightOffBottomTop)    
  {
    stopBottomTop();
    sync2RealLife();
  }

  if (needToLightOnTopBottom)     
  {
    startTopBottom();
    sync2RealLife();
  }
  
  if (need2LightOffTopBottom)
  {  
    stopTopBottom();
    sync2RealLife();
  }
}

void startBottomTop(){  //процедура ВКЛючения снизу вверх

  unsigned long currStairTime = millis();
  if (currStairTime - prevStairTime >= deltaStair) 
  {
    prevStairTime = currStairTime;
    
    if (currStair < stairsCount-1)
    {
      currStair++;
      stairsToProcess[currStair]=1;
      needToPrintStair=false;

    }
  }     

  unsigned long currBrigtnessTime = millis();
  if (currBrigtnessTime - prevBrigtness >= deltaBrightness) 
  {
    prevBrigtness = currBrigtnessTime;
    for(int i=0;i<stairsCount;i++)
    {
      if (stairsToProcess[i]==1 && stairsArray[i]<5)
      {
        stairsArray[i]++;
      }
      if (stairsToProcess[i]==1 && stairsArray[i]==5) 
      {
        stairsToProcess[i]=0;
        needToPrintBrightness=false;

      }
    }
  }

  if (needToPrintStair || needToPrintBrightness)
  {
    if (needToPrintStair) debug("Stair ");
    if (needToPrintBrightness) debug("Bright ");

    print_pixels(); 
    needToPrintStair=false;
    needToPrintBrightness=false;
  }


  if ((currStair == stairsCount-1) && (stairsArray[stairsCount-1] == 5) && (!allLEDsAreOn))   //если полностью включена последняя требуемая ступенька
  {
    allLEDsAreOnTime = millis();      //сохраним время начала состояния "все ступеньки включены"
    allLEDsAreOn = true;              //флаг, все ступеньки включены
    direction = 0;                    //для последующего гашения ступенек снизу вверх
    needToLightOnBottomTop = false;    //поскольку шаг - последний, сбрасываем за собой флаг необходимости
    
    debug("stair on done: ");

    print_pixels();                           
  }  
}


void startTopBottom()
{  

  unsigned long currStairTime = millis();
  if (currStairTime - prevStairTime >= deltaStair) 
  {
    prevStairTime = currStairTime;

    if (currStair > 0)
    {
      currStair--;
      stairsToProcess[currStair]=1;
      needToPrintStair=false;

    }
  }     

  unsigned long currBrigtnessTime = millis();
  if (currBrigtnessTime - prevBrigtness >= deltaBrightness) 
  {
    prevBrigtness = currBrigtnessTime;
    for(int i=0;i<stairsCount;i++)
    {
      if (stairsToProcess[i]==1 && stairsArray[i]<5)
      {
        stairsArray[i]++;
      }
      if (stairsToProcess[i]==1 && stairsArray[i]==5) 
      {
        stairsToProcess[i]=0;
        needToPrintBrightness=false;

      }
    }
  }

  if (needToPrintStair || needToPrintBrightness)
  {
    if (needToPrintStair) debug("Stair ");
    if (needToPrintBrightness) debug("Bright ");

    print_pixels(); 
    needToPrintStair=false;
    needToPrintBrightness=false;
  }

  if ((currStair == 0) && (stairsArray[0] == 5) && (!allLEDsAreOn))   //если полностью включена последняя требуемая ступенька
  {
    allLEDsAreOnTime = millis();      //сохраним время начала состояния "все ступеньки включены"
    allLEDsAreOn = true;              //флаг, все ступеньки включены
    direction = 1;                    //для последующего гашения ступенек снизу вверх
    needToLightOnTopBottom = false;    //поскольку шаг - последний, сбрасываем за собой флаг необходимости
    
    debug("stair on done: ");

    print_pixels();                           
  }  
}

void stopBottomTop()    //процедура ВЫКЛючения снизу вверх
{
  if (allLEDsAreOn) allLEDsAreOn = false; 
  
  unsigned long currStairTime = millis();
  if (currStairTime - prevStairTime >= deltaStair) 
  {
    prevStairTime = currStairTime;

    if (currStair < stairsCount-1)
    {
      currStair++;
      stairsToProcess[currStair]=1;
      needToPrintStair=false;

    }
  }     

  unsigned long currBrigtnessTime = millis();
  if (currBrigtnessTime - prevBrigtness >= deltaBrightness) 
  {
    prevBrigtness = currBrigtnessTime;
    for(int i=0;i<stairsCount;i++)
    {
      if (stairsToProcess[i]==1 && stairsArray[i]>0)
      {
        stairsArray[i]--;
      }
      if (stairsToProcess[i]==1 && stairsArray[i]==0) 
      {
        stairsToProcess[i]=0;
        needToPrintBrightness=false;

      }
    }
  }

  if (needToPrintStair || needToPrintBrightness)
  {
    if (needToPrintStair) debug("Stair ");
    if (needToPrintBrightness) debug("Bright ");

    print_pixels(); 
    needToPrintStair=false;
    needToPrintBrightness=false;
  }


  if ((currStair == stairsCount-1) && (stairsArray[stairsCount-1] == 0))   //если полностью включена последняя требуемая ступенька
  {
    need2LightOffBottomTop = false;    //поскольку шаг - последний, сбрасываем за собой флаг необходимости
    
    debug("stair off done: ");

    print_pixels();                           
  }  
}



void stopTopBottom() //процедура ВЫКЛючения сверху вниз
{   
  if (allLEDsAreOn) allLEDsAreOn = false; 
  
  unsigned long currStairTime = millis();
  if (currStairTime - prevStairTime >= deltaStair) 
  {
    prevStairTime = currStairTime;

    if (currStair > 0)
    {
      currStair--;
      stairsToProcess[currStair]=1;
      needToPrintStair=false;

    }
  }     

  unsigned long currBrigtnessTime = millis();
  if (currBrigtnessTime - prevBrigtness >= deltaBrightness) 
  {
    prevBrigtness = currBrigtnessTime;
    for(int i=0;i<stairsCount;i++)
    {
      if (stairsToProcess[i]==1 && stairsArray[i]>0)
      {
        stairsArray[i]--;
      }
      if (stairsToProcess[i]==1 && stairsArray[i]==0) 
      {
        stairsToProcess[i]=0;
        needToPrintBrightness=false;
        //sync2RealLife();
      }
    }
  }

  if (needToPrintStair || needToPrintBrightness)
  {
    if (needToPrintStair) debug("Stair ");
    if (needToPrintBrightness) debug("Bright ");


    print_pixels(); 
    needToPrintStair=false;
    needToPrintBrightness=false;
  }


  if ((currStair == 0) && (stairsArray[0] == 0))   //если полностью включена последняя требуемая ступенька
  {
    need2LightOffTopBottom = false;    //поскольку шаг - последний, сбрасываем за собой флаг необходимости
    
    debug("stair off done: ");

    print_pixels();                           
  }  
}

void sensorPrepare(byte sensorNo){    //процедура первоначальной "инициализации" сенсора
#if (sensorType == 1)
  if (sensorNo == 1){
    pinMode(sonar1trig, OUTPUT);
    pinMode(sonar1echo, INPUT);
    pinMode(sonar1resetpin, OUTPUT);
    digitalWrite(sonar1resetpin, HIGH);   //всегда должен быть HIGH, для перезагрузки сонара кратковременно сбросить в LOW
  }
  else if (sensorNo == 2){
    pinMode(sonar2trig, OUTPUT);
    pinMode(sonar2echo, INPUT);
    pinMode(sonar2resetpin, OUTPUT);
    digitalWrite(sonar2resetpin, HIGH);   //всегда должен быть HIGH, для перезагрузки сонара кратковременно сбросить в LOW
  }
#elif (sensorType == 2)
  pinMode(pir1SignalPin, INPUT);
  pinMode(pir2SignalPin, INPUT);
#endif


}//procedure

void sonarReset(byte sonarNo){          //процедура ресета подвисшего сонара, на 100 мс "отбирает" у него питание
  if (sonarNo == 1){
    digitalWrite(sonar1resetpin, LOW);
    delay(100);
    digitalWrite(sonar1resetpin, HIGH);
  }
  else if (sonarNo == 2){
    digitalWrite(sonar2resetpin, LOW);
    delay(100);
    digitalWrite(sonar2resetpin, HIGH);
  }//if
}//procedure

void sensorDisable(byte sensorNo){        //процедура "запрета" сенсора
  if (sensorNo == 1) sensor1previousTime = millis();
  if (sensorNo == 2) sensor2previousTime = millis();
}

boolean sensorEnabled(byte sensorNo){     //функция, дающая знать, не "разрешен" ли уже сенсора

  if ((sensorNo == 1)&&((sensor1previousTime + sensorInactiveTime) <= millis())) return true;
  else
    if ((sensorNo == 2)&&((sensor2previousTime + sensorInactiveTime) <= millis())) return true;
    else 
      return false;
}

boolean sensorTrigged(byte sensorNo){     //процедура проверки, сработал ли сенсора (с отслеживанием "подвисания" сонаров)
  char serial_key;
  //aSerial.pln("read_sensor");
  if (Serial.available() > 0) 
  {
    serial_key = Serial.read();
    if (serial_key=='1') { force_trigger_1 = true; debug("GetKey 1");}
    if (serial_key=='2') { force_trigger_2 = true; debug("GetKey 2");}
  }

#if (sensorType == 2) // PIR
  if (sensorNo == 1 && sensorEnabled(1))
  {
    
    if (digitalRead(pir1SignalPin) == HIGH || force_trigger_1) 
    { 
      if (force_trigger_1) debug("trigger_1");
      if (ignoreSensor1Count > 0) 
      {  //если требуется 1 раз проигнорить сонар 2
        debug("ignored ");
        debug(ignoreSensor1Count);
        ignoreSensor1Count--;        //проигнорили, сбрасываем за собой флаг необходимости
        sensorDisable(1);            //временно "запретить" сонар, ведь по факту он сработал (хотя и заигнорили), иначе каждые 400мс будут "ходить всё новые люди"
        force_trigger_1=false;
        return false;
      }
      sensorDisable(1);              //временно "запретить" сонар, иначе каждые 400мс будут "ходить всё новые люди"
      force_trigger_1=false;
      debug("triggered ");
      return true;
    }
    else 
      return false;
  }
  if (sensorNo == 2 && sensorEnabled(2)) 
  {
    
    if (digitalRead(pir2SignalPin) == HIGH || force_trigger_2) 
    {
      if (force_trigger_2) debug("trigger_2");
      if (ignoreSensor2Count > 0) 
      {  //если требуется 1 раз проигнорить сонар 2
        debug("ignored ");
        debug(ignoreSensor2Count);
        ignoreSensor2Count--;        //проигнорили, сбрасываем за собой флаг необходимости
        sensorDisable(2);            //временно "запретить" сонар, ведь по факту он сработал (хотя и заигнорили), иначе каждые 400мс будут "ходить всё новые люди"
        force_trigger_2=false;
        return false;
      }
      sensorDisable(2);              //временно "запретить" сонар, иначе каждые 400мс будут "ходить всё новые люди"
      force_trigger_2=false;
      debug("triggered 2");
      return true;
    }
    else 
      return false;
  }
#elif (sensorType == 1) 
  if ((sensorNo == 1)&&(sensorEnabled(1))){
    digitalWrite(sonar1trig, LOW);
    delayMicroseconds(5);
    digitalWrite(sonar1trig, HIGH);
    delayMicroseconds(15);
    digitalWrite(sonar1trig, LOW);
    unsigned int time_us = pulseIn(sonar1echo, HIGH, 5000); //5000 - таймаут, то есть не ждать сигнала более 5мс
    unsigned int distance = time_us / 58;
    if ((distance != 0)&&(distance <= sonar1minLimit))
    {     //сонар считается сработавшим, принимаем "меры"

      if (ignoreSensor1Count > 0) {  //если требуется 1 раз проигнорить сонар 2
        ignoreSensor1Count--;        //проигнорили, сбрасываем за собой флаг необходимости
        sensorDisable(1);            //временно "запретить" сонар, ведь по факту он сработал (хотя и заигнорили), иначе каждые 400мс будут "ходить всё новые люди"
        return false;
      }
      sensorDisable(1);              //временно "запретить" сонар, иначе каждые 400мс будут "ходить всё новые люди"
      return true;
    }
    else if (distance == 0){        //сонар 1 "завис"
      #if (useResetMechanism)
        sonarReset(1);
      #endif
      return false;
    }
    else return false;
  }

  if ((sensorNo == 2)&&(sensorEnabled(2))){
    digitalWrite(sonar2trig, LOW);
    delayMicroseconds(5);
    digitalWrite(sonar2trig, HIGH);
    delayMicroseconds(15);
    digitalWrite(sonar2trig, LOW);
    unsigned int time_us = pulseIn(sonar2echo, HIGH, 5000); //5000 - таймаут, то есть не ждать сигнала более 5мс
    unsigned int distance = time_us / 58;
    if ((distance != 0)&&(distance <= sonar2minLimit)){     //сонар считается сработавшим, принимаем "меры"

      if (ignoreSensor2Count > 0) {  //если требуется 1 раз проигнорить сонар 2
        ignoreSensor2Count--;        //проигнорили, сбрасываем за собой флаг необходимости
        sensorDisable(2);            //временно "запретить" сонар, ведь по факту он сработал (хотя и заигнорили), иначе каждые 400мс будут "ходить всё новые люди"
        return false;
      }
      sensorDisable(2);              //временно "запретить" сонар, иначе каждые 400мс будут "ходить всё новые люди"
      return true;
    }
    else if (distance == 0){        //сонар 2 "завис"
      #if (useResetMechanism)
        sonarReset(2);
      #endif
      return false;
    }//endelse
    else return false;
  }//if sensor 2
#endif
}//procedure


// Create a 24 bit color value from R,G,B
uint32_t Color(byte r, byte g, byte b)
{
  //у меня //B,G,R!!!
  uint32_t c;
  c = b;
  c <<= 8;
  c |= g;
  c <<= 8;
  c |= r;
  return c;
}

void fade1(int start, int count, byte r,byte g,byte b, float fade_amount)
{
  //for (int i=0; i < strip.numPixels(); i++) strip.setPixelColor(i, Color(0,0,0));
  for(int i=start;i<start+count;i++)
  {
    uint32_t color;
    float fade_q = abs(i-(start+count/2))*fade_amount;
    
    if (fade_q!=0 )
    {
      color = Color(r/fade_q,g/fade_q,b/fade_q);
    }
    else 
      color=Color(r,g,b);
      
    strip.setPixelColor(i,  color); 
  }

}

void clear1(int start, int count, byte r,byte g,byte b)
{
  for (int i=0; i < strip.numPixels(); i++) strip.setPixelColor(i, Color(0,0,0));
  strip.show();
}

void sync2RealLife()   //процедуры синхронизации "фантазий" массива с "реальной жизнью"
{  
  int color=0;
  byte r,g,b;
  for (int i = 0; i < stairsCount; i++) 
  {
    if ((i==0  || i==stairsCount-1) && stairsArray[i]==0)//Первую и последнюю не гасим полностью
    {
      r=255/30;
      g=160/30;
      b=0/20;
      fade1(i*ledsinBlock,ledsinBlock,r,g,b,0);
    }
    else
    {
      r=255/5*stairsArray[i];
      g=160/5*stairsArray[i];
      b=0/5*stairsArray[i];
      fade1(i*ledsinBlock,ledsinBlock,r,g,b,4);
    }
  }  
  strip.show();  


}

void print_pixels()
{
  #ifdef DEBUG
    debug("");
    aSerial.p("\tToProcess: ");
    for (byte i=0; i<stairsCount; i++)
      aSerial.p(stairsToProcess[i]).p(" "); 
  
    aSerial.p("\tProcessed: ");
    for (byte i=0; i<stairsCount; i++)
      aSerial.p(stairsArray[i]).p(" ");
      
    aSerial.pln();
  #endif
}

void debug(String msg)
{
  #ifdef DEBUG
    aSerial.p(msg).p("\t").p(millis()).pln();
  #endif
}
void debug(double value)
{
  #ifdef DEBUG
    aSerial.p(value).p("\t").p(millis()).pln();
  #endif
}

