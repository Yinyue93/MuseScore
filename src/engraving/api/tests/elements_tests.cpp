/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <gtest/gtest.h>

#include <QJSEngine>

#include "engraving/api/v1/apistructs.h"
#include "engraving/api/v1/cursor.h"
#include "engraving/api/v1/elements.h"
#include "engraving/api/v1/part.h"
#include "engraving/api/v1/score.h"

#include "engraving/dom/chord.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/instrchange.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/part.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/spacer.h"
#include "engraving/dom/tie.h"
#include "engraving/dom/volta.h"

#include "engraving/tests/utils/scorerw.h"

using namespace muse;
using namespace mu::engraving;

static const String CURSOR_DATA_DIR("cursor_data/");

// empty.mscx with the given quarter-note pitches at the start of m1
static MasterScore* readScoreWithQuarters(const std::vector<int>& pitches)
{
    MasterScore* score = ScoreRW::readScore(CURSOR_DATA_DIR + u"empty.mscx");
    if (!score) {
        return nullptr;
    }

    apiv1::Cursor cursor(score);
    cursor.rewind(apiv1::Cursor::SCORE_START);
    cursor.setDuration(1, 4);
    score->startCmd(TranslatableString::untranslatable("Engraving API elements tests"));
    for (int pitch : pitches) {
        cursor.addNote(pitch);
    }
    score->endCmd();

    return score;
}

static mu::engraving::Note* noteAt(Score* score, int chordIndex)
{
    Segment* s = score->firstMeasure()->first(SegmentType::ChordRest);
    for (int i = 0; i < chordIndex && s; ++i) {
        s = s->next(SegmentType::ChordRest);
    }
    EngravingItem* e = s ? s->element(0) : nullptr;
    return (e && e->isChord()) ? toChord(e)->upNote() : nullptr;
}

//---------------------------------------------------------
//   Ties
//---------------------------------------------------------

TEST(Engraving_ApiElementsTests, addTieToNextNote)
{
    MasterScore* score = readScoreWithQuarters({ 60, 60 });
    ASSERT_TRUE(score);
    mu::engraving::Note* first = noteAt(score, 0);
    mu::engraving::Note* second = noteAt(score, 1);
    ASSERT_TRUE(first && second);

    apiv1::Note apiNote(first, apiv1::Ownership::SCORE);
    score->startCmd(TranslatableString::untranslatable("Engraving API elements tests"));
    apiv1::Tie* apiTie = apiNote.addTie();
    score->endCmd();

    ASSERT_TRUE(apiTie);
    ASSERT_TRUE(first->tieFor());
    EXPECT_EQ(first->tieFor()->endNote(), second);
    EXPECT_EQ(second->tieBack(), first->tieFor());

    delete score;
}

TEST(Engraving_ApiElementsTests, addTieToGivenNote)
{
    MasterScore* score = readScoreWithQuarters({ 60, 62, 60 });
    ASSERT_TRUE(score);
    mu::engraving::Note* first = noteAt(score, 0);
    mu::engraving::Note* third = noteAt(score, 2);
    ASSERT_TRUE(first && third);

    apiv1::Note apiNote(first, apiv1::Ownership::SCORE);
    apiv1::Note apiEnd(third, apiv1::Ownership::SCORE);
    score->startCmd(TranslatableString::untranslatable("Engraving API elements tests"));
    apiNote.addTie(&apiEnd);
    score->endCmd();

    ASSERT_TRUE(first->tieFor());
    EXPECT_EQ(first->tieFor()->endNote(), third);

    delete score;
}

TEST(Engraving_ApiElementsTests, addNewTieElement)
{
    // newElement(TIE) + note.add() used to add a tie without an end note and crash layout
    MasterScore* score = readScoreWithQuarters({ 60, 60 });
    ASSERT_TRUE(score);
    mu::engraving::Note* first = noteAt(score, 0);
    mu::engraving::Note* second = noteAt(score, 1);
    ASSERT_TRUE(first && second);

    mu::engraving::Tie* tie = Factory::createTie(score->dummy());
    apiv1::Tie* apiTie = new apiv1::Tie(tie, apiv1::Ownership::PLUGIN);
    apiv1::Note apiNote(first, apiv1::Ownership::SCORE);

    score->startCmd(TranslatableString::untranslatable("Engraving API elements tests"));
    apiNote.add(apiTie);
    score->endCmd();

    EXPECT_EQ(apiTie->ownership(), apiv1::Ownership::SCORE);
    EXPECT_EQ(first->tieFor(), tie);
    EXPECT_EQ(tie->endNote(), second);

    delete apiTie;
    delete score;
}

TEST(Engraving_ApiElementsTests, addNewTieElementWithoutEndNote)
{
    MasterScore* score = readScoreWithQuarters({ 60, 62 });
    ASSERT_TRUE(score);
    mu::engraving::Note* first = noteAt(score, 0);
    ASSERT_TRUE(first);

    mu::engraving::Tie* tie = Factory::createTie(score->dummy());
    apiv1::Tie* apiTie = new apiv1::Tie(tie, apiv1::Ownership::PLUGIN);
    apiv1::Note apiNote(first, apiv1::Ownership::SCORE);

    score->startCmd(TranslatableString::untranslatable("Engraving API elements tests"));
    apiNote.add(apiTie);
    score->endCmd();

    // refused: the tie stays with the plugin and the note is untouched
    EXPECT_EQ(apiTie->ownership(), apiv1::Ownership::PLUGIN);
    EXPECT_FALSE(first->tieFor());

    delete apiTie;
    delete score;
}

//---------------------------------------------------------
//   Volta endings
//---------------------------------------------------------

TEST(Engraving_ApiElementsTests, setVoltaEndings)
{
    MasterScore* score = readScoreWithQuarters({});
    ASSERT_TRUE(score);

    {
        Volta* volta = Factory::createVolta(score->dummy());
        apiv1::EngravingItem apiVolta(volta, apiv1::Ownership::PLUGIN); // deletes the volta

        apiVolta.set(Pid::VOLTA_ENDING, QVariantList { 1, 2 });
        EXPECT_EQ(volta->endings(), std::vector<int>({ 1, 2 }));

        QJSEngine engine;
        apiVolta.set(Pid::VOLTA_ENDING, QVariant::fromValue(engine.evaluate("[3]")));
        EXPECT_EQ(volta->endings(), std::vector<int>({ 3 }));

        apiVolta.set(Pid::VOLTA_ENDING, 4);
        EXPECT_EQ(volta->endings(), std::vector<int>({ 4 }));
    }

    delete score;
}

//---------------------------------------------------------
//   Spacers
//---------------------------------------------------------

static apiv1::Spacer* addSpacer(MasterScore* score, mu::engraving::SpacerType type)
{
    mu::engraving::Spacer* spacer = Factory::createSpacer(score->dummy());
    apiv1::Spacer* apiSpacer = new apiv1::Spacer(spacer, apiv1::Ownership::PLUGIN);
    apiSpacer->setSpacerType(int(type));

    apiv1::Cursor cursor(score);
    cursor.rewind(apiv1::Cursor::SCORE_START);
    score->startCmd(TranslatableString::untranslatable("Engraving API elements tests"));
    cursor.add(apiSpacer);
    score->endCmd();

    return apiSpacer;
}

TEST(Engraving_ApiElementsTests, addSpacersOfEachType)
{
    MasterScore* score = readScoreWithQuarters({});
    ASSERT_TRUE(score);
    mu::engraving::Measure* m = score->firstMeasure();

    apiv1::Spacer* up = addSpacer(score, mu::engraving::SpacerType::UP);
    apiv1::Spacer* down = addSpacer(score, mu::engraving::SpacerType::DOWN);

    EXPECT_EQ(m->vspacerUp(0), up->spacer());
    EXPECT_EQ(m->vspacerDown(0), down->spacer());

    // the plugin API lookups return the spacers, wrapped as Spacer
    apiv1::Measure apiMeasure(m);
    apiv1::Spacer* apiUp = qobject_cast<apiv1::Spacer*>(apiMeasure.vspacerUp(0));
    apiv1::Spacer* apiDown = qobject_cast<apiv1::Spacer*>(apiMeasure.vspacerDown(0));
    ASSERT_TRUE(apiUp && apiDown);
    EXPECT_EQ(apiUp->spacerType(), int(mu::engraving::SpacerType::UP));
    EXPECT_EQ(apiDown->spacerType(), int(mu::engraving::SpacerType::DOWN));

    // the type of a spacer in the score cannot change
    apiDown->setSpacerType(int(mu::engraving::SpacerType::UP));
    EXPECT_EQ(down->spacer()->spacerType(), mu::engraving::SpacerType::DOWN);

    delete up;
    delete down;
    delete apiUp;
    delete apiDown;
    delete score;
}

TEST(Engraving_ApiElementsTests, addFixedSpacer)
{
    MasterScore* score = readScoreWithQuarters({});
    ASSERT_TRUE(score);

    apiv1::Spacer* fixed = addSpacer(score, mu::engraving::SpacerType::FIXED);
    mu::engraving::Spacer* s = score->firstMeasure()->vspacerDown(0);
    ASSERT_TRUE(s);
    EXPECT_EQ(s->spacerType(), mu::engraving::SpacerType::FIXED);

    delete fixed;
    delete score;
}

//---------------------------------------------------------
//   Instrument changes
//---------------------------------------------------------

TEST(Engraving_ApiElementsTests, replaceInstrumentAtTick)
{
    MasterScore* score = readScoreWithQuarters({});
    ASSERT_TRUE(score);
    mu::engraving::Part* part = score->parts().front();
    const String originalId = part->instrument()->id();
    mu::engraving::Measure* m2 = score->firstMeasure()->nextMeasure();
    ASSERT_TRUE(m2);
    const Fraction tick = m2->tick();

    // add an instrument change at the start of m2, as a plugin would
    InstrumentChange* ic = Factory::createInstrumentChange(score->dummy(), *part->instrument());
    apiv1::EngravingItem* apiIc = new apiv1::EngravingItem(ic, apiv1::Ownership::PLUGIN);
    apiv1::Cursor cursor(score);
    cursor.rewind(apiv1::Cursor::SCORE_START);
    cursor.nextMeasure();
    score->startCmd(TranslatableString::untranslatable("Engraving API elements tests"));
    cursor.add(apiIc);
    score->endCmd();

    apiv1::Score apiScore(score);
    apiv1::Part apiPart(part, apiv1::Ownership::SCORE);
    apiv1::Fraction apiTick(tick);
    score->startCmd(TranslatableString::untranslatable("Engraving API elements tests"));
    apiScore.replaceInstrumentAtTick(&apiPart, &apiTick, "harp");
    score->endCmd();

    EXPECT_EQ(part->instrument(tick)->id(), u"harp");
    EXPECT_EQ(part->instrument(Fraction(0, 1))->id(), originalId);
    EXPECT_EQ(ic->instrument()->id(), u"harp");

    delete apiIc;
    delete score;
}

TEST(Engraving_ApiElementsTests, replaceInstrumentAtTickWithoutInstrumentChange)
{
    MasterScore* score = readScoreWithQuarters({});
    ASSERT_TRUE(score);
    mu::engraving::Part* part = score->parts().front();
    const String originalId = part->instrument()->id();
    const Fraction tick = score->firstMeasure()->nextMeasure()->tick();

    apiv1::Score apiScore(score);
    apiv1::Part apiPart(part, apiv1::Ownership::SCORE);
    apiv1::Fraction apiTick(tick);
    score->startCmd(TranslatableString::untranslatable("Engraving API elements tests"));
    apiScore.replaceInstrumentAtTick(&apiPart, &apiTick, "harp");
    score->endCmd();

    EXPECT_EQ(part->instrument(tick)->id(), originalId);

    delete score;
}
