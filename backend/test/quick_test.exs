defmodule QuickTest do
  use ExUnit.Case
  doctest Quick

  test "greets the world" do
    assert Quick.hello() == :world
  end
end
