import iroha.protocol.Commands;
import iroha.protocol.TransactionOuterClass;
import iroha.protocol.Endpoint;
import iroha.protocol.Queries;
import iroha.protocol.Queries.Query;
import iroha.protocol.Queries.GetAssetInfo;

import iroha.protocol.QueryService_v1Grpc;
import iroha.protocol.QueryService_v1Grpc.QueryService_v1BlockingStub;
import iroha.protocol.CommandService_v1Grpc;
import iroha.protocol.CommandService_v1Grpc.CommandService_v1BlockingStub;
import iroha.protocol.Endpoint.TxStatus;
import iroha.protocol.Endpoint.TxStatusRequest;
import iroha.protocol.Endpoint.ToriiResponse;
import iroha.protocol.QryResponses.QueryResponse;
import iroha.protocol.QryResponses.AssetResponse;
import iroha.protocol.QryResponses.Asset;

import com.google.protobuf.InvalidProtocolBufferException;
import io.grpc.ManagedChannel;
import io.grpc.ManagedChannelBuilder;
import com.google.protobuf.ByteString;
import com.google.protobuf.Descriptors.FieldDescriptor;

import java.nio.file.Paths;
import java.nio.file.Files;
import java.io.IOException;
import java.math.BigInteger;
import java.lang.Thread;
import java.util.*;

import jp.co.soramitsu.iroha.*;

class TransactionExample {
    static {
        try {
            System.loadLibrary("irohajava");
        } catch (UnsatisfiedLinkError e) {
            System.err.println("Native code library failed to load. \n" + e);
            System.exit(1);
        }
    }

//--------------------------------------------------------------------------
    private static ModelCrypto crypto = new ModelCrypto();
    private static ModelTransactionBuilder txBuilder = new ModelTransactionBuilder();
    private static ModelQueryBuilder queryBuilder = new ModelQueryBuilder();

    public static CommandService_v1BlockingStub stub;
    public static QueryService_v1BlockingStub queryStub;

    // admin
    public static Keypair admin_keys;
    public static String admin_account_id = "admin@test";

    // alice
    public static Keypair alice_keys;
    public static String alice_account_id = "alice@com";

    // alice
    public static Keypair bob_keys;
    public static String bob_account_id = "bob@com";

    public static long startQueryCounter = 1;
//--------------------------------------------------------------------------

    public static byte[] toByteArray(ByteVector blob) {
        byte bs[] = new byte[(int)blob.size()];
        for (int i = 0; i < blob.size(); ++i) {
            bs[i] = (byte)blob.get(i);
        }
        return bs;
    }

    public static String readKeyFromFile(String path) {
        try {
            return new String(Files.readAllBytes(Paths.get(path)));
        } catch (IOException e) {
            System.err.println("Unable to read key files.\n " + e);
            System.exit(1);
        }
        return "";
    }

    public static void sendTx(UnsignedTx utx, Keypair keypair) {
      // sign transaction and get its binary representation (Blob)
      ByteVector txblob = new ModelProtoTransaction(utx).signAndAddSignature(keypair).finish().blob();

      // Convert ByteVector to byte array
      byte bs[] = toByteArray(txblob);

      // create proto object
      TransactionOuterClass.Transaction protoTx = null;
      try {
          protoTx = TransactionOuterClass.Transaction.parseFrom(bs);
      } catch (InvalidProtocolBufferException e) {
          System.err.println("Exception while converting byte array to protobuf:" + e.getMessage());
          System.exit(1);
      }
      // Send transaction to iroha
      stub.torii(protoTx);

      // wait to ensure transaction was processed
      try {
          Thread.sleep(10_000);
      }
      catch(InterruptedException ex) {
          Thread.currentThread().interrupt();
      }

      // create status request
      System.out.println("Hash of the transaction: " + utx.hash().hex());

      ByteVector txhash = utx.hash().blob();
      byte bshash[] = toByteArray(txhash);

      TxStatusRequest request = TxStatusRequest.newBuilder().setTxHash(ByteString.copyFrom(bshash)).build();
      ToriiResponse response = stub.status(request);
      String status = response.getTxStatus().name();

      System.out.println("Status of the transaction is: " + response.toString());

      if (!status.equals("COMMITTED")) {
          System.err.println("Your transaction wasn't committed");
          System.exit(1);
      }
    }

    public static QueryResponse sendQuery(UnsignedQuery uquery, Keypair keys) {
      ByteVector queryBlob = new ModelProtoQuery(uquery).signAndAddSignature(keys).finish().blob();
      byte bquery[] = toByteArray(queryBlob);

      Query protoQuery = null;
      try {
          protoQuery = Query.parseFrom(bquery);
      } catch (InvalidProtocolBufferException e) {
          System.err.println("Exception while converting byte array to protobuf:" + e.getMessage());
          System.exit(1);
      }

      QueryResponse queryResponse = queryStub.find(protoQuery);

      System.out.println(queryResponse);
      return queryResponse;
    }

    public static void prepare() {
      // build transaction (still unsigned)
      UnsignedTx utx = txBuilder.creatorAccountId(admin_account_id)
          .createdTime(BigInteger.valueOf(System.currentTimeMillis()))
          .createDomain("com", "user")
          .createAsset("dollar", "com", (short)2)
          .createAccount("alice", "com", alice_keys.publicKey())
          .addAssetQuantity("dollar#com", "100.00")
          .transferAsset("admin@test", "alice@com", "dollar#com", "", "100.00")
          .build();
      sendTx(utx, admin_keys);
    }

    public static void setAliceSignatoryAndQuorum() {
      // build transaction (still unsigned)
      UnsignedTx utx = txBuilder.creatorAccountId(alice_account_id)
          .createdTime(BigInteger.valueOf(System.currentTimeMillis()))
          .addSignatory(alice_account_id, bob_keys.publicKey())
          .setAccountQuorum("alice@com", (short)2)
          .build();
      sendTx(utx, alice_keys);
    }

    public static void createMstTransaction() {
      // build transaction (still unsigned)
      UnsignedTx utx = txBuilder.creatorAccountId(alice_account_id)
          .createdTime(BigInteger.valueOf(System.currentTimeMillis()))
          .quorum((short)2)
          .transferAsset("alice@com", "admin@test", "dollar#com", "", "100.00")
          .build();
      sendTx(utx, alice_keys);
    }

    public static void getPendingTransactions() {
      // query result of transaction we've just sent
      UnsignedQuery uquery = queryBuilder.creatorAccountId(alice_account_id)
          .queryCounter(BigInteger.valueOf(startQueryCounter++))
          .createdTime(BigInteger.valueOf(System.currentTimeMillis()))
          .getPendingTransactions()
          .build();
      QueryResponse response = sendQuery(uquery, bob_keys);

      List<TransactionOuterClass.Transaction> txs = response.getTransactionsPageResponse().getTransactionsList();
      for (TransactionOuterClass.Transaction tx : txs) {
        // jp.co.soramitsu.iroha.Transaction ptx = new jp.co.soramitsu.iroha.Transaction(tx);
        // UnsignedTx utx = new UnsignedTx(ptx);
        new Blob(tx.SerializeToArray());
      }
    }

    public static void main(String[] args) {
        admin_keys = crypto.convertFromExisting(readKeyFromFile("../admin@test.pub"),
            readKeyFromFile("../admin@test.priv"));

        alice_keys = crypto.convertFromExisting(readKeyFromFile("alice@com.pub"),
                    readKeyFromFile("alice@com.priv"));

        bob_keys = crypto.convertFromExisting(readKeyFromFile("bob@com.pub"),
                            readKeyFromFile("bob@com.priv"));

        ManagedChannel channel = ManagedChannelBuilder.forAddress("localhost", 50051).usePlaintext(true).build();
        stub = CommandService_v1Grpc.newBlockingStub(channel);
        queryStub = QueryService_v1Grpc.newBlockingStub(channel);

         prepare();
        // setAliceSignatoryAndQuorum();
        // createMstTransaction();
        getPendingTransactions();
    }
}
