/*
 * Per Clock configurations
 *
 *
 * COPY THIS TO myConfig.h and adjust your keys 
 */


const char TSauth[] = "your TS auth key";
const char TSWriteApiKey[] = "yout TS write key";
const long unsigned int TSChannelID = channelID;
const char nodeName[] = "iClock2";
const char awakeString[] = "ko_house/sensor/iClock2/awake";
const char connectString[] = "ko_house/sensor/iClock2/connect";
const char tempTopic[] = "ko_house/sensor/iClock2/board_temp";
const char inTopic[] = "ko_house/sensor/iClock2/inTopic";
const char* mqtt_server = "korpi.local";
const int  tempPubInterval = 30;
const char *ow_key      = "your OWM key";
const char *servername = "api.openweathermap.org";