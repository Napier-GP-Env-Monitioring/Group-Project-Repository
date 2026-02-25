CREATE DATABASE sensorMetadata

USE sensorMetadata

CREATE TABLE photon (
    id INT AUTO_INCREMENT PRIMARY KEY,
    curStatus VARCHAR(8),
    curLocation VARCHAR(60)
)