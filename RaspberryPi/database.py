# A database that runs on a Maria DB server whilst flask is running, purely to store sensor metadata.

from mysql import connector

# MariaDB
db = connector.connect(
    host = "localhost",
    user = "lora",
    password = "12345678",
    database = "lora_buffer"
)

cursor = db.cursor()

