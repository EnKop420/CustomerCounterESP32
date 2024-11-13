/** 
* @file main.cpp
*
* @mainpage Customer Count Project
*
* @section description Description
* A small program that when touched it writes it to a .csv file.
* It can then show a graph on a hosted website on the count of costumer for a specific date.
* Also has a WiFi Manager that can take a SSID and a Password input to connect to that WiFi.
*
* @section circuit Materials
* - 1 ESP32 Wroom Dev Kit
* - 1 Breadboard
* - 1 Wire
* - 5x5cm of tin foil
*
* @section files Files
* - customer-list.csv
* - favicon.png
* - styles.css
* - index.html
* - services.html
* - wifimanager.html
* - pass.txt
* - ssid.txt
* - temp.csv
*
* @section libraries Libraries
* - LittleFS
* - WiFi
* - time
* - ESPAsyncWebServer
* - AsyncTCP
* - WiFiUdp.h
* - ArduinoJson
*
* @section author Author
* - Created by Rasmus Wiell.
*/

#include <Arduino.h>
#include "LittleFS.h"
#include <WiFi.h>
#include <time.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Variables for NTP Time
/**
 * @brief NTP server to get time from
 */
const char* ntpServer = "pool.ntp.org";
/**
 * @brief GMT offset in seconds
 */
const long  gmtOffset_sec = 3600;
/**
 * @brief Daylight saving time offset in seconds
 */
const int   daylightOffset_sec = 3600;

/**
 * @brief Search for parameter in HTTP POST request
 */
const char* PARAM_INPUT_1 = "ssid";
const char* PARAM_INPUT_2 = "pass";

/**
 * @brief File paths to save input values permanently
 */
const char* ssidPath = "/ssid.txt";
const char* passPath = "/pass.txt";
const char* csvPath = "/customer-list.csv";

/**
 * @brief Variables to save values from HTML form
 */
String ssid;
String pass;
String ip;
String gateway;

/** 
 * @brief Touch pin and threshold 
 */
int touchPin = 4;
int threshold = 20;
bool isTouched = false;

/** 
 * @brief Boolean to check if the ESP is connected to a WiFi 
 */
bool isConnectedWiFi = false;

/**
 * @brief Is the LocalIP of the ESP32
 */
IPAddress localIP;

/**
 * @brief Set your Gateway IP address
 */
IPAddress localGateway;
IPAddress subnet(255, 255, 0, 0);

/**
 * @brief Timer variables
 */
unsigned long previousMillis = 0;
const long interval = 10000;  ///< interval to wait for Wi-Fi connection (milliseconds)

/**
 * @brief Initialize LittleFS
 */
void initLittleFS() {
  if (!LittleFS.begin(true)) {
    Serial.println("An error has occurred while mounting LittleFS");
  }
  Serial.println("LittleFS mounted successfully");
}

/**
 * @brief Read File from LittleFS
 * @param fs File system to read from
 * @param path File path to read
 * @return String file content
 */
String readConfigFiles(fs::FS &fs, const char * path){
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if(!file || file.isDirectory()){
    Serial.println("- failed to open file for reading");
    return String();
  }
  
  String fileContent;
  while(file.available()){
    fileContent = file.readStringUntil('\n');
    break;     
  }
  return fileContent;
}

/**
 * @brief Read CSV file from LittleFS
 * @param fs File system to read from
 * @param path CSV file path
 * @return CSV content as a string
 */
const char* readCsvFile(fs::FS &fs, const char * path){
  Serial.printf("Reading file: %s\r\n", path);  // Log file reading attempt

  File file = fs.open(path);  // Open the file for reading
  if(!file || file.isDirectory()){  // Check if file is open and not a directory
    Serial.println("- failed to open file for reading");
    return "";  // Return empty string if failed to open
  }

  String fileContent;
  while(file.available()){  // Read the file content
    fileContent = file.readString();   
  }
  return fileContent.c_str();  // Return the content of the file
}

/**
 * @brief Write data to file on LittleFS
 * @param fs File system to write to
 * @param path File path to write to
 * @param message Data to write
 */
void writeToConfigFiles(fs::FS &fs, const char * path, const char * message){
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE); // Opens file path in write.
  if(!file){ // Checks if file opened successfully.
    Serial.println("- failed to open file for writing");
    return;
  }
  if(file.print(message)){ // Checks if printing the "message" to file was successful or not.
    Serial.println("- file written");
  } else {
    Serial.println("- write failed");
  }
}

/**
 * @brief Appends a value to a CSV file with current date and current time as parameter.
 * @param path Path to the CSV file
 * @param currentDate Current date string
 * @param currentTime Current time string
 * @return true if success, false otherwise
 */
bool appendToCSV(const char * path, String currentDate, String currentTime) {
  // Open the CSV file in append mode
  File file = LittleFS.open(path, FILE_APPEND);
  if(!file){  // Check if the file opened successfully
    Serial.println("- failed to open file for writing");
    return false;
  }

  // Write the header if the file is empty
  if (file.size() == 0) {  // If the file is empty, add a header
    file.println("customer,date,time");
  }

  // Format and write the data
  String dataLine = "1," + currentDate + "," + currentTime;  // Prepare data line
  file.println(dataLine);  // Write the data to the file

  file.close();  // Close the file
  Serial.println("Line "+ dataLine +" appended to CSV file successfully!");  // Log success
  return true;  // Return success
}

/**
 * @brief Removes the latest value where the date is the "targetDate"
 * @param path Path to the CSV file
 * @param targetDate Date to match for removal
 * @return true if success, false otherwise
 */
bool removeLatestEntryOnDate(const char * path, String targetDate) {
  // Open the CSV file in read mode
  File file = LittleFS.open(path, FILE_READ);
  if(!file){
    Serial.println("- failed to open file for writing");
    return false;
  }

  // Create a temporary file to store the modified data
  File tempFile = LittleFS.open("/temp.csv", FILE_WRITE);
  if (!tempFile) {
    Serial.println("Failed to open temporary file for writing");
    file.close();
    return false;
  }

  // Read the CSV file line by line
  String line;
  bool foundLatest = false;
  while (file.available()) {
    line = file.readStringUntil('\n');

    // Extract date from the line
    int commaIndex1 = line.indexOf(',');
    int commaIndex2 = line.indexOf(',', commaIndex1 + 1);
    String currentDate = line.substring(commaIndex1 + 1, commaIndex2);

    // If the current date matches the target date and the latest flag is not set,
    // skip writing this line to the temporary file
    if (currentDate == targetDate && !foundLatest) {
      foundLatest = true;
      continue;
    }

    // Write the line to the temporary file
    tempFile.println(line);
  }

  // Close both files
  file.close();
  tempFile.close();

  // Delete the original CSV file
  LittleFS.remove(path);

  // Rename the temporary file to the original CSV file
  LittleFS.rename("/temp.csv", path);

  Serial.println("Latest entry on " + targetDate + " removed successfully!");
  return true;
}

/**
 * @brief Removes every line that has the inputDate
 * @param path Path to the CSV file
 * @param inputDate Date to remove lines with
 * @return true if success, false otherwise
 */
bool removeLinesWithDate(const char* path, const String& inputDate) {
  // Open the CSV file for reading
  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return false;
  }

  // Temporary storage for modified content
  String modifiedContent = "";

  // Read file line-by-line
  while (file.available()) {
    String line = file.readStringUntil('\n');

    // Skip empty lines or header (optional based on CSV structure)
    if (line.length() == 0 || line.startsWith("customer")) {
      modifiedContent += line + "\n";
      continue;
    }

    // Find the first comma to get to the date part
    int firstCommaIdx = line.indexOf(',');
    if (firstCommaIdx == -1) {
      Serial.println("Malformed CSV line; skipping: " + line);
      continue; // Skip lines without a comma
    }

    // Extract the date (between the first and second comma)
    int secondCommaIdx = line.indexOf(',', firstCommaIdx + 1);
    if (secondCommaIdx == -1) {
      Serial.println("Malformed CSV line; skipping: " + line);
      continue; // Skip lines without a second comma
    }

    // The date part is between the first and second comma
    String date = line.substring(firstCommaIdx + 1, secondCommaIdx);
    date.trim(); // Remove any extra whitespace around the date

    // Only add lines to modified content if the date doesn't match inputDate
    if (date != inputDate) {
      modifiedContent += line + "\n";
    }
  }

  file.close(); // Close the file after reading

  // Re-open the file for writing (overwrites existing content)
  file = LittleFS.open(path, "w");
  if (!file) {
    Serial.println("Failed to open file for writing");
    return false;
  }

  // Write the modified content back to the file
  file.print(modifiedContent);
  file.close();

  Serial.println("Lines with date " + inputDate + " have been removed from the CSV.");
  return true;
}

/**
 * @brief Clears a file. This makes it empty but does not delete it!
 * @param path Path to the file to clear
 * @return true if success, false otherwise
 */
bool clearFile(const char* path) {
  // Open the file in write mode, which empties it immediately
  File file = LittleFS.open(path, "w");
  if (!file) {
    Serial.println("Failed to open file for clearing"); // If File didn't exist or it failed to open.
    return false;
  }
  
  file.close();  // Close the file after opening in write mode (now empty)
  Serial.println("File has been cleared successfully.");
  return true;
}

/**
 * @brief Initializes and connects to Wi-Fi using the provided SSID and password.
 * 
 * This function sets up the Wi-Fi in station mode, configures the IP address, 
 * and attempts to connect to the specified Wi-Fi network. If the connection fails 
 * within a set timeout interval, it returns `false` and retries. On successful connection, 
 * it returns `true` and stores the IP address.
 * 
 * @return bool `true` if Wi-Fi is successfully connected, `false` otherwise.
 */
bool initWiFi() {
  if(ssid==""){  // Check if SSID is undefined
    Serial.println("Undefined SSID or IP address.");
    return false;
  }

  WiFi.mode(WIFI_STA);  // Set WiFi mode to station (STA)

  if (!WiFi.config(localIP, localGateway, subnet)){  // Configure static IP
    Serial.println("STA Failed to configure");
    return false;
  }
  WiFi.begin(ssid.c_str(), pass.c_str());  // Start WiFi connection
  Serial.println("Connecting to WiFi...");

  unsigned long currentMillis = millis();  // Get current time
  previousMillis = currentMillis;

  while(WiFi.status() != WL_CONNECTED) {  // Wait for connection
    currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {  // Timeout check
      Serial.println("Failed to connect.");
      return false;
    }
  }

  Serial.println(WiFi.localIP());  // Print local IP address after connection
  isConnectedWiFi = true;  // Set WiFi connected flag
  return isConnectedWiFi;  // Return connection status
}

/**
 * @brief Counts occurrences of each date in a CSV string.
 * @param csv The CSV data as a C-string.
 * @return JSON string with dates and their counts.
 */
String countDates(const char* csv) {
  const int MAX_DATES = 50; // Maximum expected unique dates
  String dates[MAX_DATES];  // Array to store dates
  int counts[MAX_DATES] = {0};  // Array to count occurrences
  int uniqueDateCount = 0;  // Counter for unique dates

  String csvString = String(csv);
  int startIdx = 0;
  while (true) {
    int lineEndIdx = csvString.indexOf('\n', startIdx);  // Find end of line
    if (lineEndIdx == -1) break;  // Break if no more lines

    String line = csvString.substring(startIdx, lineEndIdx);  // Extract line
    startIdx = lineEndIdx + 1;

    if (line.length() == 0 || line.startsWith("customer")) continue; // Skip empty or header lines

    // Extract date from the line
    int commaIdx = line.indexOf(',');
    int secondCommaIdx = line.indexOf(",", commaIdx + 1);
    String date = line.substring(commaIdx + 1, secondCommaIdx);


    // Check if the date is already in the list
    bool found = false;
    for (int i = 0; i < uniqueDateCount; i++) {
      if (dates[i] == date) {
        counts[i]++;  // Increment count if date is found
        found = true;
        break;
      }
    }

    // Add new date if not found
    if (!found) {
      dates[uniqueDateCount] = date;
      counts[uniqueDateCount] = 1;
      uniqueDateCount++;
    }
  }

  // Convert to JSON
  JsonDocument jsonDoc;
  for (int i = 0; i < uniqueDateCount; i++) {
    jsonDoc[dates[i]] = counts[i];
  }

  String jsonOutput;
  serializeJson(jsonDoc, jsonOutput);
  return jsonOutput; // Store each date with its count
}

/**
 * @brief Returns the time in "hh:mm" format.
 * @param timeInfo The time information.
 * @return Formatted time string.
 */
String getTime(tm timeInfo){
  // Format time as hh:mm (24-hour format)
  char timeStr[6];   // 5 characters (hh:mm) + null terminator
  strftime(timeStr, sizeof(timeStr), "%H:%M", &timeInfo);
  String time = String(timeStr);

  return time;
}

/**
 * @brief Returns the date in "yyyy/mm/dd" format.
 * @param timeInfo The date information.
 * @return Formatted date string.
 */
String getDate(tm timeInfo){
  // Format date as dd/mm/yyyy
  char dateStr[11];  // Format requires 10 characters (yyyy/mm/dd + null terminator)
  strftime(dateStr, sizeof(dateStr), "%Y/%m/%d", &timeInfo);
  String date = String(dateStr);

  return date;
}

/**
 * @brief Handles touch event and appends the current date and time to the CSV file.
 */
void onTouch() {
  if (isTouched) return;  // Ignore if already touched

  // Get current time in Danish timezone
  time_t now = time(nullptr);
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo)) {  // Handle failure to obtain time
    Serial.println("Failed to obtain time");
    return;
  }

  String date = getDate(timeInfo);  // Get formatted date
  String time = getTime(timeInfo);  // Get formatted time

  appendToCSV(csvPath, date, time);  // Save date and time to CSV
}

/**
 * @brief This is the setup function and where the majority of the code will be executed.
 */
void setup() {
  // Initialize serial communication at 115200 baud rate
  Serial.begin(115200);

  // Initialize the LittleFS filesystem
  initLittleFS();

  // Load SSID and password from saved configuration files
  ssid = readConfigFiles(LittleFS, ssidPath);
  pass = readConfigFiles(LittleFS, passPath);

  // Debug: print the loaded WiFi credentials
  Serial.println("---------WiFi Configs Read--------");
  Serial.println(ssid);
  Serial.println(pass);
  Serial.println("------------------------------");


  // Attempt to connect to WiFi using the loaded credentials
  if (initWiFi()) {
    // Notify successful connection to WiFi
    Serial.println("Connected to WiFi: " + ssid);

    /** 
     * @brief Configures the time using NTP server after successful WiFi connection.
     * @note This sets up the time zone and daylight saving settings.
     */
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    struct tm timeinfo;
    // Wait for time to be successfully obtained from NTP server
    while (!getLocalTime(&timeinfo)) {
      delay(1000); // Retry every second
      Serial.println("Waiting for time...");
    }
    Serial.println("Retrieved Time");
    // Initialize CORS headers for HTTP requests
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

    /** 
     * @brief Route for the root (/) web page
     * @details This serves the index.html file from LittleFS when accessed via HTTP GET request.
     */
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(LittleFS, "/index.html", String(), false);
    });

    /** 
     * @brief Route to load services.html page
     * @details This serves the services.html file from LittleFS when accessed via HTTP GET request.
     */
    server.on("/services.html", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(LittleFS, "/services.html", String(), false);
    });
    
    /** 
     * @brief Route to load the style.css file
     * @details This serves the style.css file from LittleFS with the appropriate MIME type for CSS when accessed via HTTP GET request.
     */
    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(LittleFS, "/style.css", "text/css");
    });

    /** 
     * @brief Handles a GET request to fetch data in JSON format
     * @details This reads the CSV file, processes the dates, and returns the date counts as a JSON response.
     */
    server.on("/get-data", HTTP_GET, [](AsyncWebServerRequest *request) {
      const char* csvData = readCsvFile(LittleFS, csvPath);
      Serial.println(csvData);
      String jsonResponse = countDates(csvData);
      Serial.println(jsonResponse);
      request->send(200, "application/json", jsonResponse);
    });

    /** 
     * @brief Adds a value like if it had been touched.
     * @details This handles a POST request to add a new entry to the CSV file with the current date and time.
     *          It returns a success or failure message based on the result of appending to the CSV.
     */
    server.on("/add-value", HTTP_POST, [](AsyncWebServerRequest *request){
      time_t now = time(nullptr);
      struct tm timeInfo;
      if (!getLocalTime(&timeInfo)) {
        Serial.println("Failed to obtain time");
        request->send(500, "text/plain", "Could not get the current date. No changes has been made.");
        return;
      }

      String date = getDate(timeInfo);
      String time = getTime(timeInfo);

      // Append the data to the CSV file
      String isSuccess = appendToCSV(csvPath, date, time) ? "Task Completed Successfully" : "Task ended up in failure.";

      request->send(200, "text/plain", isSuccess);
    });
    
    /** 
     * @brief Removes the latest value with the current date.
     * @details This handles a DELETE request to remove the most recent entry from the CSV file that matches the current date.
     *          Returns a success or failure message based on the result.
     */
    server.on("/remove-value", HTTP_DELETE, [](AsyncWebServerRequest *request){
      time_t now = time(nullptr);
      struct tm timeInfo;
      if (!getLocalTime(&timeInfo)) {
        Serial.println("Failed to obtain time");
        request->send(500, "text/plain", "Could not get the current date. No changes has been made.");
        return;
      }

      String date = getDate(timeInfo);

      String isSuccess = removeLatestEntryOnDate(csvPath, date) ? "Task Completed Successfully" : "Task ended up in failure.";

      request->send(200, "text/plain", isSuccess);
    });

    /** 
     * @brief Clears the entire customer-list.csv file.
     * @details This handles a DELETE request to clear all content in the CSV file.
     *          Returns a success or failure message based on the result of clearing the file.
     */
    server.on("/clear-csv", HTTP_DELETE, [](AsyncWebServerRequest *request){
      String isSuccess = clearFile(csvPath) ? "Task Completed Successfully" : "Task ended up in failure.";
      request->send(200, "text/plain", isSuccess);
    });

    /** 
     * @brief Clears all lines where the date is the same as today's date.
     * @details This handles a DELETE request to remove all entries from the CSV file matching today's date.
     *          Returns a success or failure message based on the result.
     */
    server.on("/clear-for-today", HTTP_DELETE, [](AsyncWebServerRequest *request){
      time_t now = time(nullptr);
      struct tm timeInfo;
      if (!getLocalTime(&timeInfo)) {
        Serial.println("Failed to obtain time");
        request->send(500, "text/plain", "Could not get the current date. No changes has been made.");
        return;
      }

      String date = getDate(timeInfo);

      String isSuccess = removeLinesWithDate(csvPath, date) ? "Task Completed Successfully" : "Task ended up in failure.";

      request->send(200, "text/plain", isSuccess);
    });

    /** 
     * @brief Clears all WiFi configurations (SSID, password, IP, and gateway).
     * @details This handles a DELETE request to clear WiFi configurations from the LittleFS storage.
     *          Restarts the device after clearing the configurations.
     */
    server.on("/clear-wifi", HTTP_DELETE, [](AsyncWebServerRequest *request){
      bool ssidCleared = clearFile(ssidPath);
      bool passCleared = clearFile(passPath);

      String isSuccess = ssidCleared && passCleared ? "Task Completed Successfully" : "Task ended up in failure.";

      request->send(200, "text/plain", isSuccess);
      Serial.println("WiFi Configs have been cleared. Will restart in 3 seconds!");
      delay(3000);
      ESP.restart();
    });
    
    /** 
     * @brief Route to download the CSV file.
     * @details This handles a GET request to allow the user to download the current CSV file from LittleFS storage.
     */
    server.on("/download-csv", HTTP_GET, [](AsyncWebServerRequest *request){
      Serial.println("Download CSV Request received!");
      request->send(LittleFS, csvPath, String(), true);
    });

    // Start the web server
    server.begin();
  }
  /**
   * @brief Creates a soft access point if the device cannot connect to the provided WiFi SSID and password.
   * 
   * @details This function sets up a WiFi access point, allowing the user to connect to it and access the WiFi Manager page. 
   * The page uses `wifimanager.html` to collect the user's input, which is then saved to text files for WiFi credentials (SSID and password).
   */
  else {
    // Connect to Wi-Fi network with SSID and password
    Serial.println("Setting AP (Access Point)");
    // NULL sets an open Access Point
    WiFi.softAP("RasmusW-Wifi-Manager", NULL);

    IPAddress IP = WiFi.softAPIP();
    Serial.println(IP);
    Serial.println("AP IP address: " + IP.toString());

    /** Web Server Root URL*/
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(LittleFS, "/wifimanager.html", "text/html");
    });
    
    server.serveStatic("/", LittleFS, "/");
    
    /**
     * @brief Handles HTTP POST requests to configure WiFi SSID and password.
     * 
     * This route listens for POST requests on the root path ("/").
     * It checks for parameters named PARAM_INPUT_1 (SSID) and PARAM_INPUT_2 (password), 
     * saves them to the respective variables, writes them to configuration files, 
     * and restarts the ESP to apply the changes.
     * 
     * @param request The incoming HTTP request.
     */
    server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) {
      int params = request->params();  // Get the number of parameters in the request
      for(int i = 0; i < params; i++) {  // Loop through all parameters
        const AsyncWebParameter* p = request->getParam(i);  // Get the parameter
        if(p->isPost()) {  // Check if the parameter is a POST value
          // Process SSID parameter
          if (p->name() == PARAM_INPUT_1) {
            ssid = p->value().c_str();  // Set the SSID
            Serial.print("SSID set to: ");
            Serial.println(ssid);
            writeToConfigFiles(LittleFS, ssidPath, ssid.c_str());  // Save SSID to file
          }
          // Process password parameter
          if (p->name() == PARAM_INPUT_2) {
            pass = p->value().c_str();  // Set the password
            Serial.print("Password set to: ");
            Serial.println(pass);
            writeToConfigFiles(LittleFS, passPath, pass.c_str());  // Save password to file
          }
        }
      }
      request->send(200, "text/plain", "Done. ESP will restart, connect to your router and go to IP address: " + ip);
      delay(3000);
      ESP.restart();
    });
    server.begin();
  }
}

/**
 * @brief Main loop of the program. Runs continuously to check the WiFi connection and handle touch input.
 * 
 * This function first checks if the device is connected to WiFi. If not, it exits early.
 * If connected, it reads the touch sensor value and checks if it is below the threshold.
 * If the threshold is met, it calls the onTouch() function and sets the isTouched flag to true.
 * If the touch value is above the threshold, it resets the isTouched flag.
 */
void loop() {
  if (!isConnectedWiFi) return;
  else
  {
      // read the state of the pushbutton value:
    int touchValue = touchRead(touchPin);
    // check if the touchValue is below the threshold
    // if it is, set ledPin to HIGH
    if(touchValue < threshold){
      onTouch();
      isTouched = true;
    }
    else if (touchValue > threshold){
      isTouched = false;
    }
  }
}