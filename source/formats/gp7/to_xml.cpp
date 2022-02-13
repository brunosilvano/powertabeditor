/*
 * Copyright (C) 2022 Cameron White
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "document.h"

#include <numeric>

using namespace std::string_literals;

/// Utility function to add a node with a CDATA child, i.e. <![CDATA[text]]>.
static void
addCDataNode(pugi::xml_node &node, const char *name, const std::string &value)
{
    node.append_child(name)
        .append_child(pugi::node_cdata)
        .set_value(value.c_str());
}

/// Utility function to add a node with a plain character data child.
static void
addValueNode(pugi::xml_node &node, const char *name, const std::string &value)
{
    node.append_child(name)
        .append_child(pugi::node_pcdata)
        .set_value(value.c_str());
}

static void
addValueNode(pugi::xml_node &node, const char *name, int value)
{
    addValueNode(node, name, std::to_string(value));
}

template <typename T>
static std::string
listToString(const std::vector<T> &items, char sep = ' ')
{
    std::string s;
    for (size_t i = 0, n = items.size(); i < n; ++i)
    {
        if (i != 0)
            s += sep;

        s += std::to_string(items[i]);
    }

    return s;
}

namespace Gp7
{
static void
saveScoreInfo(pugi::xml_node &node, const ScoreInfo &info)
{
    addCDataNode(node, "Title", info.myTitle);
    addCDataNode(node, "SubTitle", info.mySubtitle);
    addCDataNode(node, "Artist", info.myArtist);
    addCDataNode(node, "Album", info.myAlbum);
    addCDataNode(node, "Words", info.myWords);
    addCDataNode(node, "Music", info.myMusic);
    addCDataNode(node, "Copyright", info.myCopyright);
    addCDataNode(node, "Tabber", info.myTabber);
    addCDataNode(node, "Instructions", info.myInstructions);
    addCDataNode(node, "Notices", info.myNotices);
}

static void
saveTracks(pugi::xml_node &gpif, const std::vector<Track> &tracks)
{
    // In the master track, record the space-separated list of track ids.
    auto master_track_node = gpif.append_child("MasterTrack");
    std::vector<int> ids(tracks.size());
    std::iota(ids.begin(), ids.end(), 0);
    addValueNode(master_track_node, "Tracks", listToString(ids));

    auto tracks_node = gpif.append_child("Tracks");
    int track_idx = 0;
    for (const Track &track : tracks)
    {
        auto track_node = tracks_node.append_child("Track");
        track_node.append_attribute("id").set_value(track_idx);

        addCDataNode(track_node, "Name", track.myName);

        // Set the instrument type. I'm not sure if this needs to be configured
        // differently for basses, etc, but this needs to be set to avoid being
        // interpreted as a drum track.
        auto inst_set = track_node.append_child("InstrumentSet");
        addValueNode(inst_set, "Type", "electricGuitar"s);
        addValueNode(inst_set, "LineCount", 5); // standard notation staff

        auto sounds_node = track_node.append_child("Sounds");
        for (const Sound &sound : track.mySounds)
        {
            auto sound_node = sounds_node.append_child("Sound");
            addCDataNode(sound_node, "Name", sound.myLabel);
            addCDataNode(sound_node, "Label", sound.myLabel);

            auto midi_node = sound_node.append_child("MIDI");
            addValueNode(midi_node, "LSB", 0);
            addValueNode(midi_node, "MSB", 0);
            addValueNode(midi_node, "Program", sound.myMidiPreset);
        }

        // Use MIDI playback.
        addValueNode(track_node, "AudioEngineState", "MIDI"s);

        auto staves_node = track_node.append_child("Staves");
        for (const Staff &staff : track.myStaves)
        {
            auto staff_node = staves_node.append_child("Staff");
            auto props_node = staff_node.append_child("Properties");

            auto capo = props_node.append_child("Property");
            capo.append_attribute("name").set_value("CapoFret");
            addValueNode(capo, "Fret", staff.myCapo);

            auto tuning = props_node.append_child("Property");
            tuning.append_attribute("name").set_value("Tuning");
            addValueNode(tuning, "Pitches", listToString(staff.myTuning));
        }

        // In Power Tab the notes are implicitly transposed down in the
        // standard notation staff.
        auto transpose = track_node.append_child("Transpose");
        addValueNode(transpose, "Chromatic", 0);
        addValueNode(transpose, "Octave", -1);

        // TODO - export chords

        ++track_idx;
    }
}

static void
saveMasterBars(pugi::xml_node &gpif, const std::vector<MasterBar> &master_bars)
{
    auto bars_node = gpif.append_child("MasterBars");

    for (const MasterBar &master_bar : master_bars)
    {
        auto bar_node = bars_node.append_child("MasterBar");

        addValueNode(bar_node, "Bars", listToString(master_bar.myBarIds));

        if (master_bar.mySection)
        {
            auto section = bar_node.append_child("Section");
            addValueNode(section, "Letter", master_bar.mySection->myLetter);
            addValueNode(section, "Text", master_bar.mySection->myText);
        }

        // Time signature - e.g. "3/4"
        {
            std::string time_sig =
                std::to_string(master_bar.myTimeSig.myBeats) + "/" +
                std::to_string(master_bar.myTimeSig.myBeatValue);
            addValueNode(bar_node, "Time", time_sig);
        }

        // Key signature
        {
            auto key_sig = bar_node.append_child("Key");
            addValueNode(key_sig, "AccidentalCount",
                         master_bar.myKeySig.myAccidentalCount *
                             (master_bar.myKeySig.mySharps ? 1 : -1));
            addValueNode(key_sig, "Mode",
                         master_bar.myKeySig.myMinor ? "Minor"s : "Major"s);
        }

        // Bar types
        if (master_bar.myDoubleBar)
            bar_node.append_child("DoubleBar");
        if (master_bar.myFreeTime)
            bar_node.append_child("FreeTime");

        if (master_bar.myRepeatStart || master_bar.myRepeatEnd)
        {
            auto node = bar_node.append_child("Repeat");
            node.append_attribute("start").set_value(master_bar.myRepeatStart);
            node.append_attribute("end").set_value(master_bar.myRepeatEnd);
            node.append_attribute("count").set_value(master_bar.myRepeatCount);
        }

        // TODO
        // - alternate endings
        // - directions
        // - tempo changes (these are actually part of the master track)
        // - fermatas
    }
}

static void
saveBars(pugi::xml_node &gpif, const std::unordered_map<int, Bar> &bars_map)
{
    auto bars_node = gpif.append_child("Bars");

    for (auto &&[id, bar] : bars_map)
    {
        auto bar_node = bars_node.append_child("Bar");
        bar_node.append_attribute("id").set_value(id);

        // Only bass / treble clefs are needed for exporting pt2 files.
        std::string clef_str =
            (bar.myClefType == Bar::ClefType::F4) ? "F4"s : "G2"s;
        addValueNode(bar_node, "Clef", clef_str);

        addValueNode(bar_node, "Voices", listToString(bar.myVoiceIds));
    }
}

static void
saveVoices(pugi::xml_node &gpif,
           const std::unordered_map<int, Voice> &voices_map)
{
    auto voices_node = gpif.append_child("Voices");

    for (auto &&[id, voice] : voices_map)
    {
        auto voice_node = voices_node.append_child("Voice");
        voice_node.append_attribute("id").set_value(id);
        addValueNode(voice_node, "Beats", listToString(voice.myBeatIds));
    }
}

static void
saveBeats(pugi::xml_node &gpif, const std::unordered_map<int, Beat> &beats_map)
{
    auto beats_node = gpif.append_child("Beats");

    for (auto &&[id, beat] : beats_map)
    {
        auto beat_node = beats_node.append_child("Beat");
        beat_node.append_attribute("id").set_value(id);

        addValueNode(beat_node, "Notes", listToString(beat.myNoteIds));

        auto rhythm = beat_node.append_child("Rhythm");
        rhythm.append_attribute("ref").set_value(beat.myRhythmId);

        if (beat.myGraceNote)
            addValueNode(beat_node, "GraceNotes", "BeforeBeat"s);

        // TODO
        // - chord ids
        // - 8va etc
        // - tremolo picking
        // - brush up/down
        // - arpeggio up/down
        // - free text
        // - whammy
    }
}

static pugi::xml_node
addNoteProperty(pugi::xml_node &props_node, const char *name)
{
    auto prop_node = props_node.append_child("Property");
    prop_node.append_attribute("name").set_value(name);
    return prop_node;
}

static void
savePitch(pugi::xml_node &props_node, const char *name,
          const Gp7::Note::Pitch &pitch)
{
    auto prop_node = addNoteProperty(props_node, name);
    auto pitch_node = prop_node.append_child("Pitch");
    addValueNode(pitch_node, "Step", std::string{ pitch.myNote });
    addValueNode(pitch_node, "Accidental", pitch.myAccidental);
    addValueNode(pitch_node, "Octave", pitch.myOctave);
}

static void
saveNotes(pugi::xml_node &gpif, const std::unordered_map<int, Note> &notes_map)
{
    auto notes_node = gpif.append_child("Notes");

    for (auto &&[id, note] : notes_map)
    {
        auto note_node = notes_node.append_child("Note");
        note_node.append_attribute("id").set_value(id);

        auto props_node = note_node.append_child("Properties");

        // String and fret.
        {
            auto prop_node = addNoteProperty(props_node, "String");
            addValueNode(prop_node, "String", note.myString);
        }
        {
            auto prop_node = addNoteProperty(props_node, "Fret");
            addValueNode(prop_node, "Fret", note.myFret);
        }

        // Record the pitch. GP ignores the note entirely if this isn't
        // present, and uses it for notation rather than computing it from the
        // tuning and string/fret.
        savePitch(props_node, "ConcertPitch", note.myConcertPitch);
        savePitch(props_node, "TransposedPitch", note.myTransposedPitch);

        // TODO
        // - palm mute
        // - muted
        // - ties
        // - ghost
        // - tapped
        // - hammeron
        // - left hand tap
        // - vibrato
        // - wide vibrato
        // - let ring
        // - accents
        // - harmonics
        // - slides
        // - trills
        // - left hand fingering
        // - bends
    }
}

static void
saveRhythms(pugi::xml_node &gpif,
            const std::unordered_map<int, Rhythm> &rhythms_map)
{
    static const std::unordered_map<int, std::string> theNoteNamesMap = {
        { 1, "Whole"s }, { 2, "Half"s }, { 4, "Quarter"s }, { 8, "Eighth" },
        { 16, "16th" },  { 32, "32nd" }, { 64, "64th" }
    };

    auto rhythms_node = gpif.append_child("Rhythms");

    for (auto &&[id, rhythm] : rhythms_map)
    {
        auto rhythm_node = rhythms_node.append_child("Rhythm");
        rhythm_node.append_attribute("id").set_value(id);

        addValueNode(rhythm_node, "NoteValue",
                     theNoteNamesMap.at(rhythm.myDuration));

        if (rhythm.myDots > 0)
        {
            auto dots_node = rhythm_node.append_child("AugmentationDot");
            dots_node.append_attribute("count").set_value(rhythm.myDots);
        }

        if (rhythm.myTupletDenom > 0)
        {
            auto tuplet_node = rhythm_node.append_child("PrimaryTuplet");
            tuplet_node.append_attribute("num").set_value(rhythm.myTupletNum);
            tuplet_node.append_attribute("den").set_value(rhythm.myTupletDenom);
        }
    }
}

pugi::xml_document
to_xml(const Document &doc)
{
    pugi::xml_document root;

    auto gpif = root.append_child("GPIF");
    addValueNode(gpif, "GPVersion", "7.6.0"s);

    auto score = gpif.append_child("Score");
    saveScoreInfo(score, doc.myScoreInfo);

    saveTracks(gpif, doc.myTracks);
    saveMasterBars(gpif, doc.myMasterBars);
    saveBars(gpif, doc.myBars);
    saveVoices(gpif, doc.myVoices);
    saveBeats(gpif, doc.myBeats);
    saveNotes(gpif, doc.myNotes);
    saveRhythms(gpif, doc.myRhythms);

    return root;
}
} // namespace Gp7
