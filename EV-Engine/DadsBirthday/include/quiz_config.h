#pragma once

/**
 *  @file quiz_config.h
 *
 *  @brief The quiz as written in game/questions.json (see DadsBirthday/README.md for the format).
 */

#include <filesystem>
#include <string>
#include <vector>

namespace Birthday
{
    // Sides as the player sees them while facing the screens.
    enum class Side
    {
        Left,
        Right,
    };

    struct QuizQuestion
    {
        std::string           question;
        std::string           leftAnswer;
        std::string           rightAnswer;
        Side                  correctSide = Side::Left;
        std::filesystem::path correctImage;  // Shown when the right bird is shot, under that bird.
        std::filesystem::path wrongImage;    // Shown when the wrong bird is shot, under that bird.
        std::filesystem::path correctImage2; // On the other screen after a right answer; empty = correctImage again.
        std::filesystem::path wrongImage2;   // On the other screen after a wrong answer; empty = wrongImage again.
        std::string           correctMessage;
        std::string           wrongMessage;
    };

    struct QuizConfig
    {
        std::string               title; // In the start menu.
        std::string               intro;
        std::vector<QuizQuestion> questions;
        std::string               finaleTitle;   // Big text on the left screen at the end.
        std::string               finaleMessage; // In the text box at the end.
        std::vector<std::filesystem::path> finaleImages; // Right screen at the end; empty = every correct image.

        std::vector<std::string> warnings; // Problems found while reading the file, shown in the game.
    };

    // Reads the quiz. Image paths are relative to the file's folder. Problems are listed in warnings; a missing or
    // broken file gives a quiz without questions that says what went wrong.
    QuizConfig LoadQuizConfig(const std::filesystem::path& file);
}
