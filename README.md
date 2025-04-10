# obs-fightrecorder
[![Build](https://github.com/JesseSwale/obs-fightrecorder/actions/workflows/push.yaml/badge.svg)](https://github.com/JesseSwale/obs-fightrecorder/actions/workflows/push.yaml)

## Introduction
An OBS plugin for the game Eve Online. The plugin detects if you are in combat and records your gameplay.

## Installation
- Before installing the plugin, make sure to enable the replay buffer functionality in Settings -> Output -> Replay buffer. 
- Please refer to the [Plugins Guide](https://obsproject.com/kb/plugins-guide) on how to install plugins for OBS.

## Build
To build this project please follow the steps [here](https://github.com/obsproject/obs-plugintemplate/wiki/Quick-Start-Guide#windows) for Windows. You need Visual Studio 2022.

## FAQ
Which files are observed?
> The files in the "Gamelogs" directory where something was logged within the last 24 hours. Per default the directory is located in `C:\Users\<user>\Documents\EVE\logs\Gamelogs`

How do you detect combat?
> I detect combat if I see either "combat" or "has applied bonuses to" in the log files.

How do I set up the replay buffer?
> obs-fightrecorder uses the Replay buffer you have configured in OBS. (Settings -> Output -> Replay buffer).

Where are the replays stored?
> The replay buffer and recording are both saved in the directory you have configured in OBS (Settings -> Output -> Recording path).

What is the grace period?
> The number of seconds the plugin will record after the last combat trigger has been seen in the log file. Every time something combat-related happens in the log file the timer gets refreshed. The default value is 120s. 

Can I concatenate (merge) the files after the combat is over?
> Not for now.