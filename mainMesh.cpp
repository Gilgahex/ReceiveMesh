#include <WiFi.h>
#include "painlessMesh.h"

#define   MESH_PREFIX       "MESHY"
#define   MESH_PASSWORD   "somethingSneaky"
#define   MESH_PORT       5555

Scheduler  userScheduler;
painlessMesh  mesh;

void receivedCallback( uint32_t from, String &msg );


//FAST LED SETUP
#define FASTLED_ALLOW_INTERRUPTS 0
#include "FastLED.h"

FASTLED_USING_NAMESPACE

#define DATA_PIN    2
//#define CLK_PIN   4
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
#define NUM_LEDS    30
CRGB leds[NUM_LEDS];

#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

#define BRIGHTNESS          60
#define FRAMES_PER_SECOND  120

 // limit my draw to 1A at 5v of power draw
//FastLED.setMaxPowerInVoltsAndMilliamps(5,1000); 

// -- The core to run FastLED.show()
#define FASTLED_SHOW_CORE 0


// -- Task handles for use in the notifications
static TaskHandle_t FastLEDshowTaskHandle = 0;
static TaskHandle_t userTaskHandle = 0;


//Helper Functions

/** show() for ESP32
 *  Call this function instead of FastLED.show(). It signals core 0 to issue a show, 
 *  then waits for a notification that it is done.
 */
void FastLEDshowESP32()
{
    if (userTaskHandle == 0) {
        // -- Store the handle of the current task, so that the show task can
        //    notify it when it's done
        userTaskHandle = xTaskGetCurrentTaskHandle();

        // -- Trigger the show task
        xTaskNotifyGive(FastLEDshowTaskHandle);

        // -- Wait to be notified that it's done
        const TickType_t xMaxBlockTime = pdMS_TO_TICKS( 200 );
        ulTaskNotifyTake(pdTRUE, xMaxBlockTime);
        userTaskHandle = 0;
    }
}

/** show Task
 *  This function runs on core 0 and just waits for requests to call FastLED.show()
 */
void FastLEDshowTask(void *pvParameters)
{
    // -- Run forever...
    for(;;) {
        // -- Wait for the trigger
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // -- Do the show (synchronously)
        FastLED.show();

        // -- Notify the calling task
        xTaskNotifyGive(userTaskHandle);
    }
}

void setup() {
  Serial.begin(115200);

  mesh.setDebugMsgTypes( ERROR | STARTUP | CONNECTION );  // set before init() so that you can see startup messages
  mesh.init( MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT, WIFI_AP_STA, 6 );
  mesh.onReceive(&receivedCallback);

  //LED SETUP

  delay(2000); // 2 second delay for recovery
  
  // tell FastLED about the LED strip configuration
  FastLED.addLeds<LED_TYPE,DATA_PIN,COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  
  // set master brightness control
  FastLED.setBrightness(BRIGHTNESS);

   int core = xPortGetCoreID();
   Serial.print("Main code running on core ");
   Serial.println(core);

    // -- Create the FastLED show task
    xTaskCreatePinnedToCore(FastLEDshowTask, "FastLEDshowTask", 2048, NULL, 2, &FastLEDshowTaskHandle, FASTLED_SHOW_CORE);
} //End Setup


void receivedCallback( uint32_t from, String &msg ) {
  Serial.printf("logServer: Received from %u msg=%s\n", from, msg.c_str());
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(msg);
  int sensorValue = root["sensorValue"];
  //int rgb[] = root["rgb"];
  Serial.println(sensorValue);
  int smoothedValue = smoother(sensorValue);

  lerp = lerp8by8(BRIGHTNESS, smoothedValue, fract8);
  smoothedValue = BRIGHTNESS;
  FastLED.setBrightness(lerp);
  fill_solid(leds, NUM_LEDS, CRGB(255,0,0) );
}

int smoother(int sensorValue){
  //1024 --> 256
  sensorValue = sensorValue / 4;
  //256 --> 100
  sensorValue = scale8(sensorValue,100);
  return sensorValue;
  }

void loop() {
  userScheduler.execute(); // it will run mesh scheduler as well
  mesh.update();


  Serial.println("Sending Output to LEDs");
  // send the 'leds' array out to the actual LED strip
  FastLEDshowESP32();
  // FastLED.show();
  // insert a delay to keep the framerate modest
  FastLED.delay(1000/FRAMES_PER_SECOND); 
}

