```mermaid
%%{init: {
    'themeVariables': {
        'fontSize': '14px'
    },
    'flowchart': {
      'padding': 10,
      'nodeSpacing': 30,
      'rankSpacing': 60,
      'curve': 'linear'
    }
}}%%

graph
    subgraph CAM["Arducam Camera"]
        SENSOR["IMX519 Sensor"]
        FOCUS["AK7375<br/>Focus Motor"]
    end

    subgraph LAB["Lower Avionics Bay"]
        MPS["Modular Power Supply"]
        FC["Flight Computer"]
    end

    subgraph PCB["Custom PCB"]
        subgraph POW["Power"]
            PMIC
        end

        subgraph Sensors
            IMU["LSM6x IMU"]
        end

        subgraph STOR["Storage"]
            SD["MicroSD<br/>Boot + Storage"]
        end

        subgraph CONTROL["Control + Processing"]
            RC["RV1106<br/>H.265 Encoder"]
            STM["STM32U0<br/>Supervisor + IMU Logger"]
        end

    end

    MPS -->|5V| PMIC
    FC <-->|UART| STM
    SENSOR -->|CSI-2| RC
    RC -->|I2C| FOCUS
    RC -->|I2C| SENSOR

    STM -->|"UART</br>Control/Data"| RC

    RC <-->|SDMMC| SD
    STM <-->|I2C| IMU

    STM -->|I2C| PMIC
```