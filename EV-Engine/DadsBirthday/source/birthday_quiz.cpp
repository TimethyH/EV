#include "core/EV.h"

#include "birthday_game.h"

#include <future>
#include <random>

#include "textures.h"

using namespace DirectX;
using namespace EV;
using namespace Birthday;

namespace
{
    // Pictures are made smaller than this (width and height) when they are loaded; phone photos are much bigger.
    constexpr uint32_t g_maxPictureSize = 1600;

    TextCard AnswerCard(const std::string& text)
    {
        TextCard card;
        card.text = ToWide(text);
        return card;
    }

    TextCard FinaleCard(const std::string& text)
    {
        TextCard card;
        card.text = ToWide(text);
        card.maxFontSize = 230.0f;
        card.topColor = XMFLOAT3(0.93f, 0.33f, 0.45f);
        card.bottomColor = XMFLOAT3(0.45f, 0.08f, 0.42f);
        card.textColor = XMFLOAT3(1.0f, 0.95f, 0.75f);
        return card;
    }

    TextCard MissingPictureCard(const std::filesystem::path& path)
    {
        TextCard card;
        card.text = L"Picture not found:\n" + path.filename().wstring();
        card.maxFontSize = 90.0f;
        card.topColor = XMFLOAT3(0.45f, 0.08f, 0.08f);
        card.bottomColor = XMFLOAT3(0.2f, 0.03f, 0.03f);
        return card;
    }
}

void BirthdayGame::LoadQuiz(CommandList& commandList)
{
    m_quiz = LoadQuizConfig(m_options.gameFolder / "questions.json");

    auto makeCard = [&commandList](const TextCard& card)
    {
        return Birthday::CreateTexture(commandList, RenderTextCard(card), L"Card: " + card.text.substr(0, 40));
    };

    for (const auto& question : m_quiz.questions)
    {
        m_answerCards.push_back(makeCard(AnswerCard(question.leftAnswer)));
        m_answerCards.push_back(makeCard(AnswerCard(question.rightAnswer)));
    }
    m_idleCards[static_cast<int>(Side::Left)] = makeCard(AnswerCard("Shoot the birthday cake"));
    m_idleCards[static_cast<int>(Side::Right)] = makeCard(AnswerCard("to start the quiz!"));
    m_finaleCard = makeCard(FinaleCard(m_quiz.finaleTitle));

    // Every picture once. They are decoded at the same time on worker threads and uploaded here.
    std::vector<std::filesystem::path> paths;
    for (const auto& question : m_quiz.questions)
    {
        paths.push_back(question.correctImage);
        paths.push_back(question.wrongImage);
    }
    paths.insert(paths.end(), m_quiz.finaleImages.begin(), m_quiz.finaleImages.end());

    std::vector<std::pair<std::filesystem::path, std::future<Birthday::Image>>> loads;
    for (const auto& path : paths)
    {
        if (path.empty() || m_pictures.count(path) > 0)
        {
            continue;
        }
        m_pictures[path] = nullptr;
        loads.emplace_back(path, std::async(std::launch::async, LoadPicture, path, g_maxPictureSize));
    }
    for (auto& [path, load] : loads)
    {
        try
        {
            m_pictures[path] = Birthday::CreateTexture(commandList, load.get(), path.filename().wstring());
        }
        catch (const std::exception&)
        {
            m_quiz.warnings.push_back("Picture not found: " + path.string());
            m_pictures[path] = makeCard(MissingPictureCard(path));
        }
    }

    // Problems with the quiz file are shown with the welcome message, so they are noticed while testing.
    std::string intro = m_quiz.intro;
    for (const auto& warning : m_quiz.warnings)
    {
        OutputDebugStringA(("questions.json: " + warning + "\n").c_str());
        intro += "\n(" + warning + ")";
    }
    m_quiz.intro = intro;

    ResetQuiz();
}

void BirthdayGame::ResetQuiz()
{
    m_state = QuizState::Intro;
    m_questionIndex = 0;
    m_correctAnswers = 0;
    m_banner.clear();
    m_confetti.clear();
    ShowIdleScreens();
    ShowMessage(m_quiz.intro);
}

std::shared_ptr<Texture> BirthdayGame::GetPicture(const std::filesystem::path& path) const
{
    const auto it = m_pictures.find(path);
    return it != m_pictures.end() ? it->second : nullptr;
}

void BirthdayGame::ShowIdleScreens()
{
    ShowOnScreen(Side::Left, { m_idleCards[static_cast<int>(Side::Left)] });
    ShowOnScreen(Side::Right, { m_idleCards[static_cast<int>(Side::Right)] });
}

void BirthdayGame::ShowMessage(const std::string& text)
{
    m_message = text;
    m_messageVisible = true;
    m_messageTime = 0.0f;
}

void BirthdayGame::OnCakeShot()
{
    switch (m_state)
    {
    case QuizState::Intro:
        if (m_quiz.questions.empty())
        {
            ShowMessage("There are no questions yet. Add them to game/questions.json.");
        }
        else
        {
            StartQuestion(0);
        }
        break;
    case QuizState::Question:
        ShowMessage("First answer the question: shoot the bird above the answer you think is right!");
        break;
    case QuizState::Answered:
        if (m_questionIndex + 1 < m_quiz.questions.size())
        {
            StartQuestion(m_questionIndex + 1);
        }
        else
        {
            StartFinale();
        }
        break;
    case QuizState::Finale:
        StartFinale(); // More confetti.
        break;
    }
}

void BirthdayGame::OnBirdShot(Side side)
{
    if (m_state == QuizState::Question)
    {
        Answer(side);
    }
}

void BirthdayGame::StartQuestion(size_t index)
{
    const QuizQuestion& question = m_quiz.questions[index];
    m_state = QuizState::Question;
    m_questionIndex = index;

    ShowOnScreen(Side::Left, { m_answerCards[index * 2] });
    ShowOnScreen(Side::Right, { m_answerCards[index * 2 + 1] });

    m_banner = question.question;
    std::string message = "Question " + std::to_string(index + 1) + " of " + std::to_string(m_quiz.questions.size()) + ":  " +
        question.question;
    if (index == 0)
    {
        message += "\nShoot the bird above the answer you think is right.";
    }
    ShowMessage(message);
}

void BirthdayGame::Answer(Side side)
{
    const QuizQuestion& question = m_quiz.questions[m_questionIndex];
    const bool correct = side == question.correctSide;
    if (correct)
    {
        ++m_correctAnswers;
    }
    m_state = QuizState::Answered;
    m_banner.clear();

    // The screen under the bird that was shot shows the picture; the other one keeps its answer.
    if (auto picture = GetPicture(correct ? question.correctImage : question.wrongImage))
    {
        ShowOnScreen(side, { picture });
    }

    const bool last = m_questionIndex + 1 >= m_quiz.questions.size();
    ShowMessage((correct ? question.correctMessage : question.wrongMessage) + "\n" +
        (last ? "Shoot the cake one more time for a surprise!" : "Shoot the cake for the next question."));
}

void BirthdayGame::StartFinale()
{
    m_state = QuizState::Finale;
    m_banner.clear();

    ShowOnScreen(Side::Left, { m_finaleCard });
    std::vector<std::shared_ptr<Texture>> pictures;
    for (const auto& path : m_quiz.finaleImages)
    {
        if (auto picture = GetPicture(path))
        {
            pictures.push_back(picture);
        }
    }
    ShowOnScreen(Side::Right, pictures);

    ShowMessage(m_quiz.finaleMessage + "\nYou got " + std::to_string(m_correctAnswers) + " of " +
        std::to_string(m_quiz.questions.size()) + " right!");

    // Confetti from the top of the screen.
    std::mt19937 random(static_cast<uint32_t>(m_time * 1000.0f) + 7u);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const ImU32 colors[] = { IM_COL32(255, 80, 90, 255), IM_COL32(255, 200, 40, 255), IM_COL32(80, 200, 255, 255),
        IM_COL32(120, 230, 120, 255), IM_COL32(230, 120, 255, 255), IM_COL32(255, 255, 255, 255) };
    for (int i = 0; i < 260; ++i)
    {
        Particle particle;
        particle.position = XMFLOAT2(unit(random), -unit(random) * 0.9f - 0.05f);
        particle.velocity = XMFLOAT2((unit(random) - 0.5f) * 0.3f, 0.05f + unit(random) * 0.1f);
        particle.angle = unit(random) * XM_2PI;
        particle.spin = (unit(random) - 0.5f) * 10.0f;
        particle.size = 5.0f + unit(random) * 6.0f;
        particle.color = colors[i % _countof(colors)];
        m_confetti.push_back(particle);
    }
}
