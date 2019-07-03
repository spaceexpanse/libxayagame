// Copyright (C) 2019 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "gamestatejson.hpp"

#include "testgame.hpp"

#include <xayautil/base64.hpp>
#include <xayautil/hash.hpp>
#include <xayautil/uint256.hpp>

#include <gtest/gtest.h>

#include <google/protobuf/text_format.h>
#include <google/protobuf/util/message_differencer.h>

#include <glog/logging.h>

#include <sstream>

using google::protobuf::TextFormat;
using google::protobuf::util::MessageDifferencer;

namespace xaya
{
namespace
{

Json::Value
ParseJson (const std::string& str)
{
  std::istringstream in(str);
  Json::Value res;
  in >> res;
  CHECK (in);

  return res;
}

/**
 * Checks if the given actual game-state JSON for a channel matches the
 * expected one, taking into account potential differences in protocol buffer
 * serialisation for the metadata and stateproof.  Those are verified by
 * comparing the protocol buffers themselves.
 */
void
CheckChannelJson (Json::Value actual, const std::string& expected,
                  const uint256& id, const proto::ChannelMetadata& meta,
                  const BoardState& reinitState,
                  const BoardState& proofState)
{
  ASSERT_EQ (actual["id"].asString (), id.ToHex ());
  actual.removeMember ("id");

  ASSERT_EQ (actual["meta"]["reinit"], EncodeBase64 (meta.reinit ()));
  actual["meta"].removeMember ("reinit");

  std::string bytes;
  ASSERT_TRUE (DecodeBase64 (actual["meta"]["proto"].asString (), bytes));
  proto::ChannelMetadata actualMeta;
  ASSERT_TRUE (actualMeta.ParseFromString (bytes));
  ASSERT_TRUE (MessageDifferencer::Equals (actualMeta, meta));
  actual["meta"].removeMember ("proto");

  ASSERT_EQ (actual["reinit"]["base64"].asString (),
             EncodeBase64 (reinitState));
  actual["reinit"].removeMember ("base64");

  ASSERT_EQ (actual["state"]["base64"].asString (), EncodeBase64 (proofState));
  actual["state"].removeMember ("base64");

  ASSERT_TRUE (DecodeBase64 (actual["state"]["proof"].asString (), bytes));
  proto::StateProof proof;
  ASSERT_TRUE (proof.ParseFromString (bytes));
  ASSERT_EQ (proof.initial_state ().data (), proofState);
  actual["state"].removeMember ("proof");

  ASSERT_EQ (actual, ParseJson (expected));
}

class GameStateJsonTests : public TestGameFixture
{

protected:

  ChannelsTable tbl;

  /** Test channel set up with state (100, 2).   */
  const uint256 id1 = SHA256::Hash ("channel 1");

  /** Metadata for channel 1.  */
  proto::ChannelMetadata meta1;

  /**
   * Test channel set up with state (50, 20), reinit state (40, 10)
   * and a dispute.
   */
  const uint256 id2 = SHA256::Hash ("channel 2");

  /** Metadata for channel 2.  */
  proto::ChannelMetadata meta2;

  GameStateJsonTests ()
    : tbl(game)
  {
    CHECK (TextFormat::ParseFromString (R"(
      participants:
        {
          name: "foo"
          address: "addr 1"
        }
      participants:
        {
          name: "bar"
          address: "addr 2"
        }
    )", &meta1));

    auto h = tbl.CreateNew (id1);
    h->Reinitialise (meta1, "100 2");
    h.reset ();

    h = tbl.CreateNew (id2);
    meta2 = meta1;
    meta2.mutable_participants (1)->set_name ("baz");
    meta2.set_reinit ("reinit id");
    h->SetDisputeHeight (55);
    h->Reinitialise (meta2, "40 10");
    proto::StateProof proof;
    proof.mutable_initial_state ()->set_data ("50 20");
    h->SetStateProof (proof);
    h.reset ();
  }

};

TEST_F (GameStateJsonTests, WithoutDispute)
{
  auto h = tbl.GetById (id1);
  CheckChannelJson (ChannelToGameStateJson (*h, game.rules), R"(
    {
      "meta":
        {
          "participants":
            [
              {"name": "foo", "address": "addr 1"},
              {"name": "bar", "address": "addr 2"}
            ]
        },
      "state":
        {
          "parsed": {"count": 2, "number": 100},
          "turncount": 2,
          "whoseturn": null
        },
      "reinit":
        {
          "parsed": {"count": 2, "number": 100},
          "turncount": 2,
          "whoseturn": null
        }
    }
  )", id1, meta1, "100 2", "100 2");
}

TEST_F (GameStateJsonTests, WithDispute)
{
  auto h = tbl.GetById (id2);
  CheckChannelJson (ChannelToGameStateJson (*h, game.rules), R"(
    {
      "disputeheight": 55,
      "meta":
        {
          "participants":
            [
              {"name": "foo", "address": "addr 1"},
              {"name": "baz", "address": "addr 2"}
            ]
        },
      "state":
        {
          "parsed": {"count": 20, "number": 50},
          "turncount": 20,
          "whoseturn": 0
        },
      "reinit":
        {
          "parsed": {"count": 10, "number": 40},
          "turncount": 10,
          "whoseturn": 0
        }
    }
  )", id2, meta2, "40 10", "50 20");
}

TEST_F (GameStateJsonTests, AllChannels)
{
  Json::Value expected(Json::objectValue);

  auto h = tbl.GetById (id1);
  expected[h->GetId ().ToHex ()] = ChannelToGameStateJson (*h, game.rules);
  h = tbl.GetById (id2);
  expected[h->GetId ().ToHex ()] = ChannelToGameStateJson (*h, game.rules);

  EXPECT_EQ (AllChannelsGameStateJson (tbl, game.rules), expected);
}

} // anonymous namespace
} // namespace xaya
