# Dad's Birthday

A birthday quiz on a beach, built on OceanRenderer's ocean.

The game opens in a start menu with a rotating birthday cake. Play fades to black and you wake up on the beach,
as if from a dream: the eyes open, blink, and you sit up looking at the sea. Behind you, on a brick promenade, are two big screens with a bird
flying above each of them and a birthday cake on a pedestal in the middle. Shoot the cake to get a question: the
left and right screen each show an answer. Shoot the bird above the answer you think is right. That screen then
shows a picture: a nice one for the right answer, something else for the wrong one. Shoot the cake again for the
next question. Behind the screens are the big CURACAO letters and a row of colorful Willemstad houses. After the last question the cake brings the finale: "Happy Birthday" on the left screen and all the
pictures together on the right one.

## Controls

| | |
|---|---|
| W A S D (or the arrow keys) | walk |
| Shift | run |
| Mouse | look around |
| Left mouse button | shoot; continue after a message |
| Right mouse button | scope in / out |
| Esc | pause (Esc again quits) |
| F11 | full screen on/off |

## Questions, answers and pictures

Everything is in `game/questions.json` (open it with Notepad). Pictures go in `game/images`.

```json
{
  "intro": "Happy birthday! ... Turn around and shoot the birthday cake to start the quiz!",
  "questions": [
    {
      "question": "Where did we go on holiday in 1998?",
      "left": "Spain",
      "right": "Italy",
      "correct": "left",
      "correctImage": "images/spain_beach.jpg",
      "wrongImage": "images/italy_rain.jpg",
      "correctMessage": "Yes! Remember the paella?",
      "wrongMessage": "No, that was the year after!"
    }
  ],
  "finale": {
    "title": "Happy Birthday Dad!",
    "message": "That was the last question. Have a wonderful day!",
    "images": []
  }
}
```

- Add as many questions as you like; they are asked in this order.
- `left` and `right` are the answers on the left and right screen, as seen when facing the screens.
- `correct` is `"left"` or `"right"`.
- `correctImage` / `wrongImage`: paths relative to the `game` folder. jpg, png, bmp, gif and tiff work. Photos of any
  size are fine (they are made smaller when loaded) and phone photos are turned the right way up.
- `correctMessage` / `wrongMessage` are optional; without them the game says "Correct! Well done." or
  "Oops, that was not the right one!".
- `finale.images` is the list of pictures on the right screen at the end, in this order. Leave it empty (`[]`) to
  use every `correctImage`. They are made as big as possible while all fitting on the screen.
- Lines starting with `//` are comments.
- A mistake in the file (a missing comma, a picture that is not there) is shown in the welcome message when the game
  starts, and a missing picture shows "Picture not found" on the screen, so test the whole quiz once.

## Running it while working on it

- Build `DadsBirthday` in `EV-Engine.slnx` and run it. It needs `EV-Engine/assets/birthday`: run
  `EV-Engine/download-birthday-assets.bat` once.
- Without arguments the game starts right away, full screen, without the editor.
- The start menu's title is `"title"` in `questions.json`; `"intro"` is the message after waking up.
- `--editor` starts in the editor (F5 plays, Esc or F5 goes back). Everything can be moved there: the screens, the
  birds (they follow their screen), the cake, the palm trees. Save the layout with File > Save Scene As... as
  `DadsBirthday/game/scene.evscene`; the game loads it when it starts.
- `--autopilot <folder>` plays a fixed tour and saves a screenshot of every step in the folder, then quits. The CI
  does this and publishes the screenshots on the `ci-screenshots` branch (`birthday/`).

## How it is built

- `BirthdayGame` (`include/birthday_game.h`) is an `EV::Ocean` (OceanRenderer's ocean, sky and lighting) that uses its
  hooks: `LoadGameContent`, `UpdateGame`, `DrawObjects`, `UpdatePointLights` and `DrawGameGUI`. The layout (sizes and
  positions) is in `Birthday::Layout` in the same header.
- `source/birthday_scene.cpp`: the beach (a heightfield that runs into the sea), the promenade, the screens, the birds,
  the cake and the palm trees.
- `source/birthday_player.cpp`: walking inside the invisible border, looking, the rifle with recoil and a muzzle flash,
  shooting (an instant ray from the crosshair), the HUD and the autopilot.
- `source/birthday_quiz.cpp` and `source/quiz_config.cpp`: the quiz.
- `source/textures.cpp`: text on the screens (drawn with Windows GDI) and loading pictures.
- Distance fog (`m_fogColor`, `m_fogDensity`, `m_fogStart` of `EV::Ocean`, also in the editor under Ocean Parameters >
  Fog) fades the far ocean and the sky below the horizon into the horizon's color, so the edge of the ocean is not seen.
- `shaders/screen_pixel.hlsl`: the screens are unlit and go through the inverse of the tonemapping curve, so pictures
  look exactly like the files.
- Assets: `assets/birthday/CREDITS.txt` (Poly Haven, Kenney and Khronos, all free to use). The
  `birthday-assets` release holds them; `.github/workflows/birthday-assets.yml` makes it.
