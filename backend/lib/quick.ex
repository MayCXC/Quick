defmodule Quick do
  @moduledoc """
  Documentation for `Quick`.
  """

  @doc """
  Hello world.

  ## Examples

      iex> Quick.hello()
      :world

  """
  require Logger

  use Application

  @impl true
  def start(_type, _args) do
    Supervisor.start_link(
      [
        { Task.Supervisor, name: Quick.TaskSupervisor },
        Supervisor.child_spec({Task, fn -> accept(4040) end}, restart: :permanent)
      ],
      strategy: :one_for_one,
      name: Quick.Supervisor
    )
  end

  def accept(port) do
    # The options below mean:
    #
    # 1. `:binary` - receives data as binaries (instead of lists)
    # 2. `packet: :line` - receives data line by line
    # 3. `active: false` - blocks on `:gen_tcp.recv/2` until data is available
    # 4. `reuseaddr: true` - allows us to reuse the address if the listener crashes
    #
    {:ok, socket} =
      :gen_tcp.listen(port, [:binary, packet: :line, active: false, reuseaddr: true])
    Logger.info("Accepting connections on port #{port}")
    loop_acceptor(socket)
  end

  defp loop_acceptor(socket) do
    {:ok, client} = :gen_tcp.accept(socket)
    Logger.info(Port.info(client))
    {:ok, pid} = Task.Supervisor.start_child(Quick.TaskSupervisor, fn -> serve(client) end)
    :ok = :gen_tcp.controlling_process(client, pid)
    loop_acceptor(socket)
  end

  defp serve(socket) do
    socket
    |> read_line()
#   |> write_line(socket)

    serve(socket)
  end

  defp read_line(socket) do
    {:ok, data} = :gen_tcp.recv(socket, 0)
    Logger.info(data)
    data
  end

#  defp write_line(line, socket) do
#    :gen_tcp.send(socket, line)
#  end
end
