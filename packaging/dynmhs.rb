class Dynmhs < Formula
  desc "Dynamic Multi-Homing Setup (DynMHS)"
  homepage "https://www.nntb.no/~dreibh/dynmhs/"
  url "https://www.nntb.no/~dreibh/dynmhs/download/dynmhs-0.3.6.tar.xz"
  sha256 "41fc5a4f53514642fcd7257f29e465b32304b9d74369ed0577e77699a4926693"
  license "GPL-3.0-or-later"

  # DynMHS relies on Linux-specific kernel routing interfaces and iproute2
  depends_on :linux

  # Mandatory build-time dependencies
  depends_on "cmake" => :build
  depends_on "ninja" => :build
  depends_on "pkg-config" => :build

  # Alphabetically sorted runtime dependencies
  depends_on "boost"
  depends_on "iproute2"

  def install
    args = std_cmake_args + %W[
      -GNinja
      -DCMAKE_INSTALL_SYSCONFDIR=#{etc}
    ]

    system "cmake", "-S", ".", "-B", "build", *args
    system "cmake", "--build", "build"
    system "cmake", "--install", "build"
  end

  service do
    run [opt_bin/"dynmhs", "--config", etc/"dynmhs/dynmhs.conf"]
    keep_alive true
    require_root true
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/dynmhs --version 2>&1")
  end
end
