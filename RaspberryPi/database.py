# A database that runs on a MariaDB server whilst flask is running, purely to store sensor metadata.

from mysql import connector

# Connect to database
db = connector.connect(
    host = "localhost",
    user = "lora",
    password = "12345678", # Change
    database = "loraBuffer"
)

cursor = db.cursor()

# --- Database Class? ---

# Read function
# Write function