#include "quiz_config.h"

#include <fstream>

#include "nlohmann/json.hpp"

using json = nlohmann::json;

namespace Birthday
{
    namespace
    {
        const char* g_defaultIntro =
            "Happy birthday! Walk with W A S D, look around with the mouse and shoot with the left mouse button. "
            "Turn around and shoot the birthday cake to start the quiz!";
        const char* g_defaultCorrect = "Goedzo Meneer Hyman!.";
        const char* g_defaultWrong = "Hmm.. volgends mij weet je beter...";

        std::string Text(const json& object, const char* key, const std::string& fallback = {})
        {
            const auto it = object.find(key);
            return it != object.end() && it->is_string() ? it->get<std::string>() : fallback;
        }

        std::filesystem::path PathFromUTF8(const std::string& utf8)
        {
            return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
        }
    }

    QuizConfig LoadQuizConfig(const std::filesystem::path& file)
    {
        QuizConfig config;
        config.title = "Happy Birthday!";
        config.intro = g_defaultIntro;
        config.finaleTitle = "Happy Birthday!";
        config.finaleMessage = "That was the last question. Happy birthday!";

        const std::filesystem::path folder = file.parent_path();
        json root;
        try
        {
            std::ifstream stream(file, std::ios::binary);
            if (!stream)
            {
                config.warnings.push_back("Could not open " + file.string());
                return config;
            }
            // Comments are allowed, so the file can explain itself.
            root = json::parse(stream, nullptr, true, true);
        }
        catch (const std::exception& e)
        {
            config.warnings.push_back(file.filename().string() + " has a mistake in it: " + e.what());
            return config;
        }

        config.title = Text(root, "title", config.title);
        config.intro = Text(root, "intro", config.intro);

        const auto questions = root.find("questions");
        if (questions != root.end() && questions->is_array())
        {
            int number = 0;
            for (const auto& entry : *questions)
            {
                ++number;
                if (!entry.is_object())
                {
                    continue;
                }
                QuizQuestion question;
                question.question = Text(entry, "question");
                question.leftAnswer = Text(entry, "left");
                question.rightAnswer = Text(entry, "right");
                const std::string correct = Text(entry, "correct", "left");
                if (correct != "left" && correct != "right")
                {
                    config.warnings.push_back("Question " + std::to_string(number) + ": \"correct\" must be \"left\" or \"right\"");
                }
                question.correctSide = correct == "right" ? Side::Right : Side::Left;

                const std::string correctImage = Text(entry, "correctImage");
                const std::string wrongImage = Text(entry, "wrongImage");
                if (!correctImage.empty())
                {
                    question.correctImage = folder / PathFromUTF8(correctImage);
                }
                if (!wrongImage.empty())
                {
                    question.wrongImage = folder / PathFromUTF8(wrongImage);
                }
                question.correctMessage = Text(entry, "correctMessage", g_defaultCorrect);
                question.wrongMessage = Text(entry, "wrongMessage", g_defaultWrong);

                if (question.question.empty())
                {
                    config.warnings.push_back("Question " + std::to_string(number) + " has no \"question\"");
                }
                config.questions.push_back(std::move(question));
            }
        }
        if (config.questions.empty())
        {
            config.warnings.push_back(file.filename().string() + " has no questions");
        }

        const auto finale = root.find("finale");
        if (finale != root.end() && finale->is_object())
        {
            config.finaleTitle = Text(*finale, "title", config.finaleTitle);
            config.finaleMessage = Text(*finale, "message", config.finaleMessage);
            const auto images = finale->find("images");
            if (images != finale->end() && images->is_array())
            {
                for (const auto& image : *images)
                {
                    if (image.is_string())
                    {
                        config.finaleImages.push_back(folder / PathFromUTF8(image.get<std::string>()));
                    }
                }
            }
        }
        if (config.finaleImages.empty())
        {
            for (const auto& question : config.questions)
            {
                if (!question.correctImage.empty())
                {
                    config.finaleImages.push_back(question.correctImage);
                }
            }
        }
        return config;
    }
}
