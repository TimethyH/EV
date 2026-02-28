@ECHO OFF
PUSHD %~dp0

SET CURL=curl
SET BASE=https://github.com/TimethyH/EV/releases/download/Assets

IF NOT EXIST Assets MKDIR Assets
CD assets

IF NOT EXIST bay2k.hdr (
    ECHO Downloading bay2k.hdr...
    %CURL% -LJO %BASE%/bay2k.hdr
)

IF NOT EXIST court.hdr (
    ECHO Downloading court.hdr...
    %CURL% -LJO %BASE%/court.hdr
)

IF NOT EXIST sky4k.hdr (
    ECHO Downloading sky4k.hdr...
    %CURL% -LJO %BASE%/sky4k.hdr
)

ECHO Done!
POPD