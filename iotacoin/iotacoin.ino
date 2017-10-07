#include <ArduinoJson.h>

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ESP8266mDNS.h>
#include <Crypto.h>
#include <SPI.h>
#include <SD.h>
#define DBG_OUTPUT_PORT Serial
const char* ssid = "InOut_Hackathon";
const char* password = "hackathon@2017";
const char* host = "iot";
int balance = 0;
long start;
byte previoushash[32] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,}; 
ESP8266WebServer server(80);
static bool hasSD = false;
File uploadFile;
const int chipSelect = 4;
void returnOK() {
  server.send(200, "text/plain", "");
}
void returnFail(String msg) {
  server.send(500, "text/plain", msg + "\r\n");
}
bool loadFromSdCard(String path){
  String dataType = "text/plain";
  if(path.endsWith("/")) path += "index.htm";
  if(path.endsWith(".src")) path = path.substring(0, path.lastIndexOf("."));
  else if(path.endsWith(".htm")) dataType = "text/html";
  else if(path.endsWith(".css")) dataType = "text/css";
  else if(path.endsWith(".js")) dataType = "application/javascript";
  else if(path.endsWith(".png")) dataType = "image/png";
  else if(path.endsWith(".gif")) dataType = "image/gif";
  else if(path.endsWith(".jpg")) dataType = "image/jpeg";
  else if(path.endsWith(".ico")) dataType = "image/x-icon";
  else if(path.endsWith(".xml")) dataType = "text/xml";
  else if(path.endsWith(".pdf")) dataType = "application/pdf";
  else if(path.endsWith(".zip")) dataType = "application/zip";
  File dataFile = SD.open(path.c_str());
  if(dataFile.isDirectory()){
    path += "/index.htm";
    dataType = "text/html";
    dataFile = SD.open(path.c_str());
  }
  if (!dataFile)
    return false;
  if (server.hasArg("download")) dataType = "application/octet-stream";
  if (server.streamFile(dataFile, dataType) != dataFile.size()) {
    DBG_OUTPUT_PORT.println("Sent less data than expected!");
  }
  dataFile.close();
  return true;
}

void addFile(){
      if(server.args() != 7)                          //Add a new line
        {
          server.send(200, "text/plain", "BAD ARG");
          return;
        }
      DBG_OUTPUT_PORT.println("Received GET request");
      String from = server.arg("from");
      String to = server.arg("to");
      String fileName = server.arg("fileName");
      String amount = server.arg("amount");
      String description = server.arg("description");
      String prevHash = server.arg("prevHash");
      String curHash = server.arg("curHash");
      
      StaticJsonBuffer<500> jsonBuffer;
      JsonObject& root = jsonBuffer.createObject();
      root["from"] = from;
      root["to"] = to;
      root["fileName"] = fileName;
      root["amount"] = amount;
      root["description"] = description;
      root["prevHash"] = prevHash;
      root["curHash"] = curHash;
      
      String groot;
      root.printTo(groot);
      File newFile = SD.open("balances.txt", FILE_WRITE);
      if(newFile){
        newFile.println(groot);
        newFile.close();
      }
      else
        DBG_OUTPUT_PORT.println("Error opening file..");
      server.send(200,"text/plain", "New data added..");
    }

void handleFileUpload(){
  if(server.uri() != "/edit") return;
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    if(SD.exists((char *)upload.filename.c_str())) return;
    uploadFile = SD.open(upload.filename.c_str(), FILE_WRITE);
    DBG_OUTPUT_PORT.print("Upload: START, filename: "); DBG_OUTPUT_PORT.println(upload.filename);
  } else if(upload.status == UPLOAD_FILE_WRITE){
    if(uploadFile) uploadFile.write(upload.buf, upload.currentSize);
    DBG_OUTPUT_PORT.print("Upload: WRITE, Bytes: "); DBG_OUTPUT_PORT.println(upload.currentSize);
  } else if(upload.status == UPLOAD_FILE_END){
    if(uploadFile) uploadFile.close();
    
    SHA256 hasher;
    byte hash[SHA256_SIZE];
    
    DBG_OUTPUT_PORT.print("Upload: END, Size: "); DBG_OUTPUT_PORT.println(upload.totalSize);
    String fileName = String(upload.filename.c_str());
    String from = WiFi.localIP().toString();
    String to = from;
    String amount = "1";
    String description = "get it from html";
    String prevHash, curHash;
    DBG_OUTPUT_PORT.println("Previous HASH:");
    for(int i=0;i<32;i++){
      DBG_OUTPUT_PORT.print(previoushash[i],HEX);
      prevHash += String(previoushash[i], HEX);
      DBG_OUTPUT_PORT.print(" ");
    }
    
    String compiled = from + to + fileName + amount + description + prevHash;
    DBG_OUTPUT_PORT.println(compiled);
    char charBuf[200];
    compiled.toCharArray(charBuf, compiled.length());
    hasher.doUpdate(charBuf);
    hasher.doFinal(hash);
    
    DBG_OUTPUT_PORT.println("CURRENT HASH:");
    for(int j=0;j<32;j++){
      DBG_OUTPUT_PORT.print(hash[j],HEX);
      curHash += String(hash[j], HEX);
      DBG_OUTPUT_PORT.print(" ");
    }
    DBG_OUTPUT_PORT.println("Sending request..");
    GETRequest("/newfile",from,to,fileName,amount,description,prevHash,curHash);
    DBG_OUTPUT_PORT.println("Request sent..");
  }
}
void deleteRecursive(String path){
  File file = SD.open((char *)path.c_str());
  if(!file.isDirectory()){
    file.close();
    SD.remove((char *)path.c_str());
    return;
  }
  file.rewindDirectory();
  while(true) {
    File entry = file.openNextFile();
    if (!entry) break;
    String entryPath = path + "/" +entry.name();
    if(entry.isDirectory()){
      entry.close();
      deleteRecursive(entryPath);
    } else {
      entry.close();
      SD.remove((char *)entryPath.c_str());
    }
    yield();
  }
  SD.rmdir((char *)path.c_str());
  file.close();
}
void handleDelete(){
  if(server.args() == 0) return returnFail("BAD ARGS");
  String path = server.arg(0);
  if(path == "/" || !SD.exists((char *)path.c_str())) {
    returnFail("BAD PATH");
    return;
  }
  deleteRecursive(path);
  returnOK();
}
void handleCreate(){
  if(server.args() == 0) return returnFail("BAD ARGS");
  String path = server.arg(0);
  if(path == "/" || SD.exists((char *)path.c_str())) {
    returnFail("BAD PATH");
    return;
  }
  if(path.indexOf('.') > 0){
    File file = SD.open((char *)path.c_str(), FILE_WRITE);
    if(file){
      file.write((const char *)0);
      file.close();
    }
  } else {
    SD.mkdir((char *)path.c_str());
  }
  returnOK();
}
void GETRequest(String getPath, String from,String to,String fileName, String amount,String description,String prevHash,String curHash)
{   String ip = "10.1.24.121";
    String port = "80";
    String path = getPath + "?from=" + from + "&to=" + to + "&fileName=" + fileName + "&amount=" + amount + "&description=" + description + "&prevHash=" + prevHash + "&curHash" + curHash;
    String url = "http://" + ip + ":" + port + path;
    Serial.println("URL is: " + url);
    if (WiFi.status() == WL_CONNECTED) { //Check WiFi connection status
    HTTPClient http;  //Declare an object of class HTTPClient
    http.begin(url);  //Specify request destination
    int httpCode = http.GET();                                                                  //Send the request
    if (httpCode > 0) { //Check the returning code
      String payload = http.getString();   //Get the request response payload
      Serial.println(payload);                     //Print the response payload
    }
    else
      Serial.println(httpCode);
    http.end();   //Close connection
  }
}  
void printDirectory() {
  if(!server.hasArg("dir")) return returnFail("BAD ARGS");
  String path = server.arg("dir");
  if(path != "/" && !SD.exists((char *)path.c_str())) return returnFail("BAD PATH");
  File dir = SD.open((char *)path.c_str());
  path = String();
  if(!dir.isDirectory()){
    dir.close();
    return returnFail("NOT DIR");
  }
  dir.rewindDirectory();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/json", "");
  WiFiClient client = server.client();
  server.sendContent("[");
  for (int cnt = 0; true; ++cnt) {
    File entry = dir.openNextFile();
    if (!entry)
    break;
    String output;
    if (cnt > 0)
      output = ',';
    output += "{\"type\":\"";
    output += (entry.isDirectory()) ? "dir" : "file";
    output += "\",\"name\":\"";
    output += entry.name();
    output += "\"";
    output += "}";
    server.sendContent(output);
    entry.close();
 }
 server.sendContent("]");
 dir.close();
}
void handleNotFound(){
  if(hasSD && loadFromSdCard(server.uri())){
    return;
  }
  DBG_OUTPUT_PORT.println("SD CARD ERROR PRINTING FLAGS");
  DBG_OUTPUT_PORT.println(hasSD);
  DBG_OUTPUT_PORT.println(loadFromSdCard(server.uri()));
  String message = "SDCARD Not Detected\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " NAME:"+server.argName(i) + "\n VALUE:" + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  DBG_OUTPUT_PORT.print(message);
}
void setup(void){
  DBG_OUTPUT_PORT.begin(115200);
  DBG_OUTPUT_PORT.setDebugOutput(true);
  DBG_OUTPUT_PORT.print("\n");
  WiFi.begin(ssid, password);
  DBG_OUTPUT_PORT.print("Connecting to ");
  DBG_OUTPUT_PORT.println(ssid);
  // Wait for connection
  uint8_t i = 0;
  while (WiFi.status() != WL_CONNECTED && i++ < 20) {//wait 10 seconds
    delay(500);
  }
  if(i == 21){
    DBG_OUTPUT_PORT.print("Could not connect to");
    DBG_OUTPUT_PORT.println(ssid);
    while(1) delay(500);
  }
  DBG_OUTPUT_PORT.print("Connected! IP address: ");
  DBG_OUTPUT_PORT.println(WiFi.localIP());
  if (MDNS.begin(host)) {
    MDNS.addService("http", "tcp", 80);
    DBG_OUTPUT_PORT.println("MDNS responder started");
    DBG_OUTPUT_PORT.print("You can now connect to http://");
    DBG_OUTPUT_PORT.print(host);
    DBG_OUTPUT_PORT.println(".local");
  }
  server.on("/newfile", addFile);
  server.on("/list", HTTP_GET, printDirectory);
  server.on("/edit", HTTP_DELETE, handleDelete);
  server.on("/edit", HTTP_PUT, handleCreate);
  server.on("/edit", HTTP_POST, [](){ returnOK(); }, handleFileUpload);
  server.on("/update", [](){
    ESPhttpUpdate.update("http://192.168.43.84/blinkesp.bin"); 
  });
  server.onNotFound(handleNotFound);
  server.begin();
  DBG_OUTPUT_PORT.println("HTTP server started");
  if (SD.begin(chipSelect)){
     DBG_OUTPUT_PORT.println("SD Card initialized.");
     hasSD = true;
  }
  start = millis();
}
void loop(void){
  server.handleClient();
}