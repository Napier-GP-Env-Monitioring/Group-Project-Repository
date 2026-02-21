
/* TO RUN:
CTRL + SHIFT + P
Particle: Configure Project For Device
*/

// Include Particle Device OS APIs
#include "Particle.h"

// Let Device OS manage the connection to the Particle Cloud
SYSTEM_MODE(AUTOMATIC);

// Show system, cloud connectivity, and application logs over USB
// View logs with CLI using 'particle serial monitor --follow'
SerialLogHandler logHandler(LOG_LEVEL_INFO);

// setup() runs once, when the device is first turned on
void setup() {
  Log.info("Setup has finished!");
}

// loop() runs over and over again, as quickly as it can execute.
// This is a very basic programming model (the superloop) - 
// for more advanced threading models, please see the Device OS documentation at docs.particle.io
void loop() {
  // Example: Publish event to cloud every 10 seconds.
  Log.info("Sending Hello World to the cloud!");
  Particle.publish("Hello world!");
  delay( 10 * 1000 ); // milliseconds and blocking - see docs for more info!
}