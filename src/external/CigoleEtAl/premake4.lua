solution "unit-vector-compression"
   configurations { "Debug", "Release" }

   project "unit-vector-compression"
      kind "ConsoleApp"
      language "C++"
      files { "**.h", "**.cpp", "**.c" }

      configuration "Debug"
         defines { "DEBUG" }
         flags { "Symbols" }

      configuration "Release"
         defines { "NDEBUG" }
         flags { "Optimize" }
