class WriteStumble : public Task
{
public:
  NS_INLINE_DECL_REFCOUNTING(WriteStumble)

  explicit WriteStumble(const nsCString& aDesc)
  : mDesc(aDesc)
  {}

  void Run() override;

  enum Partition {
    Begining,
    Middle,
    End,
    Unknown
  };
  static int sUploadFileNumber;

private:
  ~WriteStumble() {}
  nsresult MoveOldestFileAsUploadFile();
  Partition SetCurrentFile();
  void WriteJSON(Partition aPart, int aFileNum);

  nsCString mDesc;
  static int sCurrentFileNumber;
};