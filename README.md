# RemoteOps: Remote System Monitoring Tool

**Student Name:** Warnakulasinghe T.N  
**Registration Number:** IT24102835  
**Module:** IE3090 - Network Programming  

## Project Overview
RemoteOps is a C-based client-server application that allows a Controller (client) to securely connect to an Agent (server) to execute system commands, transfer files, and monitor system resources in real-time. It uses a concurrent POSIX thread architecture, utilizing TCP for reliable command execution and a secondary UDP thread for continuous background telemetry streaming.

## Included Files
* `agent_835.c`: The server-side agent daemon listening on personalized TCP port 9410.
* `controller_835.c`: The client-side controller interface.
* `Makefile_835`: Build script to compile the agent and controller.
* `IT24102835_Assignment_RemoteOps.pdf`: The complete implementation report and testing evidence.

## Compilation
Compile the source code by running the provided Makefile:
`make -f Makefile_835`

## Execution
1. **Start the Agent** (Listens on port 9410):
`./agent_835`

2. **Start the Controller** (Run in a separate terminal):
`./controller_835`

## Command Reference
Upon connecting to the Agent, the session is locked. You must authenticate using your token before issuing system commands.

* `AUTH OPS-2835`: Authenticates the session and unlocks command execution.
* `SYSINFO`: Retrieves server CPU load, memory usage, and uptime.
* `EXEC <CMD>`: Executes a whitelisted system command (`DATE`, `UPTIME`, `DISKFREE`, `HOSTNAME`, `WHOAMI`).
* `LISTPROC`: Retrieves a comma-separated list of the top 30 running processes.
* `PUT <filename> <size>`: Uploads a file to the Agent's `./agentfiles/IT24102835` directory.
* `GET <filename>`: Downloads a requested file from the Agent's storage directory.
* `MONITOR START <udp_port>`: Spawns a background thread to stream system stats via UDP.
* `MONITOR STOP`: Gracefully terminates the background UDP monitoring thread.
* `QUIT`: Disconnects the Controller and terminates the session.
