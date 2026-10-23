@ECHO OFF
REM Downloads the assets of the DadsBirthday game (sky, sand, bricks, rifle, cake, palm trees, duck) into assets\birthday.
PUSHD %~dp0

IF EXIST assets\birthday\CREDITS.txt (
    ECHO The birthday assets are already there.
    GOTO :done
)

ECHO Downloading birthday-assets.zip...
curl -L -o birthday-assets.zip https://github.com/TimethyH/EV/releases/download/birthday-assets/birthday-assets.zip || GOTO :failed
tar -xf birthday-assets.zip || GOTO :failed
DEL birthday-assets.zip
ECHO Done!
GOTO :done

:failed
ECHO Downloading the birthday assets failed.

:done
POPD
