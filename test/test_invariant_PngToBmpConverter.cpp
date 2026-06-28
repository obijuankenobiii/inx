#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <fstream>
#include <cstdio>
#include <cstdlib>

class SecurityTest : public ::testing::TestWithParam<std::string> {};

TEST_P(SecurityTest, BufferReadsNeverExceedDeclaredLength) {
    // Invariant: Buffer reads never exceed the declared length
    std::string payload = GetParam();
    
    // Create a temporary file with the payload
    std::string temp_filename = "test_payload.png";
    std::ofstream temp_file(temp_filename, std::ios::binary);
    temp_file.write(payload.c_str(), payload.size());
    temp_file.close();
    
    // Call the actual production function
    // Note: Assuming the main function is called convertPngToBmp
    // and takes input/output filenames as arguments
    std::string output_filename = "test_output.bmp";
    std::string command = "./PngToBmpConverter " + temp_filename + " " + output_filename;
    
    // Execute the command and capture exit code
    int result = system(command.c_str());
    
    // Clean up temporary files
    std::remove(temp_filename.c_str());
    std::remove(output_filename.c_str());
    
    // Assert no crash occurred (exit code should be normal)
    // For POSIX systems, WIFEXITED should be true and exit code != 128+SIGSEGV
    ASSERT_TRUE(WIFEXITED(result));
    EXPECT_NE(WEXITSTATUS(result), 139); // 128 + 11 (SIGSEGV)
}

INSTANTIATE_TEST_SUITE_P(
    AdversarialInputs,
    SecurityTest,
    ::testing::Values(
        // Exact exploit case: PNG with malicious width/height causing overflow
        std::string("\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\xFF\xFF\x00\x00\xFF\xFF\x08\x06\x00\x00\x00", 33),
        // Boundary case: Maximum valid PNG dimensions (2^31-1)
        std::string("\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x7F\xFF\xFF\xFF\x7F\xFF\xFF\xFF\x08\x06\x00\x00\x00", 33),
        // Valid input: Minimal PNG (1x1 grayscale)
        std::string("\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00IEND\xAE\x42\x60\x82", 67)
    )
);

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}