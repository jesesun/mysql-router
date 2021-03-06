/*
  Copyright (c) 2016, Oracle and/or its affiliates. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "test/helpers.h"

////////////////////////////////////////
// Third-party include files
#include "gtest/gtest.h"

////////////////////////////////////////
// Standard include files

#ifdef _WIN32
#include <aclapi.h>
#else
#include <sys/stat.h>
#endif

#include "keyring/keyring_file.h"
#include "common.h"


constexpr char kAesKey[] = "AesKey";
constexpr char kKeyringFileName[] = "keyring_config";


/**
 * Generic keyring test.
 *
 * Covers tests common for `KeyringMemory` and `KeyringFile`.
 */
template <typename T>
class KeyringTest : public ::testing::Test {
 public:
  KeyringTest() = default;
};

using KeyringTestTypes = ::testing::Types<mysql_harness::KeyringMemory,
                                          mysql_harness::KeyringFile>;
TYPED_TEST_CASE(KeyringTest, KeyringTestTypes);


/**
 * Deletes a file.
 *
 * @param[in] file_name Name of the file to be deleted.
 *
 * @except std::exception Failed to delete the file.
 */
static void delete_file(const std::string& file_name) {
#ifdef _WIN32

  if (DeleteFileA(file_name.c_str()) == 0) {
    auto error = GetLastError();

    if (error != ERROR_FILE_NOT_FOUND) {
      throw std::runtime_error("DeleteFile() failed: " +
                               std::to_string(error));
    }
  }

#else

  if (unlink(file_name.c_str()) != 0 && errno != ENOENT) {
    throw std::runtime_error("unlink() failed: " +
                             std::to_string(errno));
  }

#endif // _WIN32
}

/**
 * KeyringFile test.
 *
 * Prepares environment for file-based keyring implementation.
 */
class KeyringFileTest : public ::testing::Test {
 public:
  KeyringFileTest() = default;

  virtual void SetUp() override {
    delete_file(kKeyringFileName);
  }
};

/**
 * Fills keyring with test data.
 *
 * @param[out] keyring Keyring to be filled with data.
 */
static void fill_keyring(mysql_harness::Keyring& keyring) {
  keyring.store("E1", "E1A1", "E1V1");
  keyring.store("E1", "E1A2", "E1V2");
  keyring.store("E2", "E2A1", "E2V1");
  keyring.store("E2", "E2A2", "E2V2");
}

/**
 * Verifies keyring contents.
 *
 * @param[in] keyring Keyring to be verified.
 */
static void verify_keyring(const mysql_harness::Keyring& keyring) {
  ASSERT_EQ(keyring.fetch("E1", "E1A1"), "E1V1");
  ASSERT_EQ(keyring.fetch("E1", "E1A2"), "E1V2");
  ASSERT_EQ(keyring.fetch("E2", "E2A1"), "E2V1");
  ASSERT_EQ(keyring.fetch("E2", "E2A2"), "E2V2");
}

TYPED_TEST(KeyringTest, StoreFetch) {
  TypeParam keyring;

  fill_keyring(keyring);
  verify_keyring(keyring);
}

TYPED_TEST(KeyringTest, AttributeOverwrite) {
  TypeParam keyring;

  keyring.store("Entry", "Attribute", "Value");
  keyring.store("Entry", "Attribute", "OtherValue");

  ASSERT_EQ(keyring.fetch("Entry", "Attribute"), "OtherValue");
}

TYPED_TEST(KeyringTest, FetchUndefinedEntry) {
  TypeParam keyring;

  fill_keyring(keyring);

  EXPECT_THROW(keyring.fetch("InvalidEntry", "Attr"), std::out_of_range);
}

TYPED_TEST(KeyringTest, FetchUndefinedAttribute) {
  TypeParam keyring;

  fill_keyring(keyring);

  EXPECT_THROW(keyring.fetch("Entry", "AttrInvalid"), std::out_of_range);
}

TYPED_TEST(KeyringTest, RemoveEntry) {
  TypeParam keyring;

  keyring.store("Entry", "Attr", "Value");
  keyring.remove("Entry");

  EXPECT_THROW(keyring.fetch("Entry", "Attr"), std::out_of_range);
}

TYPED_TEST(KeyringTest, RemoveAttribute) {
  TypeParam keyring;

  keyring.store("Entry", "Attr", "Value");
  keyring.remove_attribute("Entry", "Attr");

  EXPECT_THROW(keyring.fetch("Entry", "Attr"), std::out_of_range);
}

TYPED_TEST(KeyringTest, SaveAndLoadEmpty) {
  std::vector<char> keyring_data;

  // Serialize empty keyring.
  {
    TypeParam keyring;

    keyring_data = keyring.serialize(kAesKey);
  }

  // Parse keyring data.
  TypeParam keyring;

  keyring.parse(kAesKey, keyring_data.data(), keyring_data.size());
}

TYPED_TEST(KeyringTest, SaveAndLoadFilled) {
  std::vector<char> keyring_data;

  // Serialize filled keyring.
  {
    TypeParam keyring;

    fill_keyring(keyring);
    keyring_data = keyring.serialize(kAesKey);
  }

  // Parse keyring data and verify contents.
  TypeParam keyring;

  keyring.parse(kAesKey, keyring_data.data(), keyring_data.size());
  verify_keyring(keyring);
}

TYPED_TEST(KeyringTest, SaveAndLoadBroken) {
  std::vector<char> keyring_data;

  // Serialize filled keyring.
  {
    TypeParam keyring;

    fill_keyring(keyring);
    keyring_data = keyring.serialize(kAesKey);
  }

  // Try loading a few randomly broken keyring buffers.
  for (std::size_t test_count = 0; test_count < 20; ++test_count) {
    TypeParam keyring;
    auto buffer_offset = static_cast<size_t>(std::rand()) % keyring_data.size();
    auto buffer_size = static_cast<size_t>(std::rand()) % (keyring_data.size() - buffer_offset + 1);

    if (buffer_offset + buffer_size == keyring_data.size()) {
      // Buffer valid, ignore.
      continue;
    }

    EXPECT_THROW(
        keyring.parse(kAesKey,
                      keyring_data.data() + buffer_offset, buffer_size),
        std::exception);
  }
}

TYPED_TEST(KeyringTest, SaveAndLoadWithInvalidKey) {
  std::vector<char> keyring_data;

  // Serialize filled keyring.
  {
    TypeParam keyring;

    fill_keyring(keyring);
    keyring_data = keyring.serialize(kAesKey);
  }

  // Parse keyring data with invalid encryption key.
  TypeParam keyring;

  EXPECT_THROW(
      keyring.parse("invalid_key", keyring_data.data(), keyring_data.size()),
      std::exception);
}

TEST_F(KeyringFileTest, LoadFromFileWithCorrectPermissions) {
  {
    mysql_harness::KeyringFile keyring;

    fill_keyring(keyring);
    keyring.save(kKeyringFileName, kAesKey);
    mysql_harness::make_file_private(kKeyringFileName);
  }

  mysql_harness::KeyringFile keyring;

  keyring.load(kKeyringFileName, kAesKey);
  verify_keyring(keyring);
}

TEST_F(KeyringFileTest, LoadFromFileWithWrongPermissions) {
  {
    mysql_harness::KeyringFile keyring;

    fill_keyring(keyring);
    keyring.save(kKeyringFileName, kAesKey);
    mysql_harness::make_file_public(kKeyringFileName);
  }

  mysql_harness::KeyringFile keyring;

  EXPECT_THROW(keyring.load(kKeyringFileName, kAesKey), std::exception);
}

TEST_F(KeyringFileTest, LoadFromNonexistentFile) {
  mysql_harness::KeyringFile keyring;

  // Setup() deletes keyring file.
  EXPECT_THROW(keyring.load(kKeyringFileName, kAesKey), std::exception);
}

int main(int argc, char *argv[]) {

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
